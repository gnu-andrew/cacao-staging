/* src/vm/jit/powerpc64/codegen.c - machine code generator for 32-bit PowerPC

   Copyright (C) 1996-2005, 2006 R. Grafl, A. Krall, C. Kruegel,
   C. Oates, R. Obermaisser, M. Platter, M. Probst, S. Ring,
   E. Steiner, C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich,
   J. Wenninger, Institut f. Computersprachen - TU Wien

   This file is part of CACAO.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   Contact: cacao@cacaojvm.org

   Authors: Andreas Krall
            Stefan Ring

   Changes: Christian Thalinger
            Christian Ullrich
            Edwin Steiner

   $Id: codegen.c 5641 2006-10-03 16:32:15Z edwin $

*/


#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <signal.h>

#include "vm/types.h"

#include "md-abi.h"
#include "vm/jit/abi-asm.h"

#include "vm/jit/powerpc64/arch.h"
#include "vm/jit/powerpc64/codegen.h"

#include "mm/memory.h"
#include "native/native.h"
#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/loader.h"
#include "vm/options.h"
#include "vm/stringlocal.h"
#include "vm/vm.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/codegen-common.h"
#include "vm/jit/dseg.h"
#include "vm/jit/emit-common.h"
#include "vm/jit/jit.h"
#include "vm/jit/parse.h"
#include "vm/jit/patcher.h"
#include "vm/jit/reg.h"
#include "vm/jit/replace.h"

#if defined(ENABLE_LSRA)
# include "vm/jit/allocator/lsra.h"
#endif


/* codegen *********************************************************************

   Generates machine code.

*******************************************************************************/

bool codegen(jitdata *jd)
{
	methodinfo         *m;
	codeinfo           *code;
	codegendata        *cd;
	registerdata       *rd;
	s4                  len, s1, s2, s3, d, disp;
	ptrint              a;
	s4                  stackframesize;
	varinfo            *var;
	basicblock         *bptr;
	instruction        *iptr;
	exceptiontable     *ex;
	u2                  currentline;
	methodinfo         *lm;             /* local methodinfo for ICMD_INVOKE*  */
	builtintable_entry *bte;
	methoddesc         *md;
	rplpoint           *replacementpoint;
	s4                  fieldtype;
	s4                  varindex;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	/* prevent compiler warnings */

	d = 0;
	lm = NULL;
	bte = NULL;

	{
	s4 i, p, t, l;
	s4 savedregs_num;

	savedregs_num = 0;

	/* space to save used callee saved registers */

	savedregs_num += (INT_SAV_CNT - rd->savintreguse);
	savedregs_num += (FLT_SAV_CNT - rd->savfltreguse);

	stackframesize = rd->memuse + savedregs_num;

#if defined(ENABLE_THREADS)
	/* space to save argument of monitor_enter and Return Values to survive */
    /* monitor_exit. The stack position for the argument can not be shared  */
	/* with place to save the return register on PPC64, since both values     */
	/* reside in R3 */
	if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
		/* reserve 2 slots for long/double return values for monitorexit */
		stackframesize += 2;
	}

#endif

	/* create method header */

	/* align stack to 16-bytes */

/* 	if (!m->isleafmethod || opt_verbosecall) */
		stackframesize = (stackframesize + 3) & ~3;

/* 	else if (m->isleafmethod && (stackframesize == LA_WORD_SIZE)) */
/* 		stackframesize = 0; */

	(void) dseg_addaddress(cd, code);                      /* CodeinfoPointer */
	(void) dseg_adds4(cd, stackframesize * 8);             /* FrameSize       */

#if defined(ENABLE_THREADS)
	/* IsSync contains the offset relative to the stack pointer for the
	   argument of monitor_exit used in the exception handler. Since the
	   offset could be zero and give a wrong meaning of the flag it is
	   offset by one.
	*/

	if (checksync && (m->flags & ACC_SYNCHRONIZED))
		(void) dseg_adds4(cd, (rd->memuse + 1) * 8);       /* IsSync          */
	else
#endif
		(void) dseg_adds4(cd, 0);                          /* IsSync          */
	                                       
	(void) dseg_adds4(cd, jd->isleafmethod);                /* IsLeaf          */
	(void) dseg_adds4(cd, INT_SAV_CNT - rd->savintreguse); /* IntSave         */
	(void) dseg_adds4(cd, FLT_SAV_CNT - rd->savfltreguse); /* FltSave         */

	dseg_addlinenumbertablesize(cd);

	(void) dseg_adds4(cd, cd->exceptiontablelength);       /* ExTableSize     */

	/* create exception table */

	for (ex = cd->exceptiontable; ex != NULL; ex = ex->down) {
		dseg_addtarget(cd, ex->start);
   		dseg_addtarget(cd, ex->end);
		dseg_addtarget(cd, ex->handler);
		(void) dseg_addaddress(cd, ex->catchtype.any);
	}
	
	/* create stack frame (if necessary) */

	if (!jd->isleafmethod) {
		M_MFLR(REG_ZERO);
		M_AST(REG_ZERO, REG_SP, LA_LR_OFFSET);
	}

	if (stackframesize)
		M_STDU(REG_SP, REG_SP, -stackframesize * 8);

	/* save return address and used callee saved registers */

	p = stackframesize;
	for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
		p--; M_LST(rd->savintregs[i], REG_SP, p * 8);
	}
	for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
		p --; M_DST(rd->savfltregs[i], REG_SP, p * 8);
	}

	/* take arguments out of register or stack frame */

	md = m->parseddesc;

 	for (p = 0, l = 0; p < md->paramcount; p++) {
 		t = md->paramtypes[p].type;
 		var = &(rd->locals[l][t]);
 		l++;
 		if (IS_2_WORD_TYPE(t))    /* increment local counter for 2 word types */
 			l++;
 		if (var->type < 0)
 			continue;
		s1 = md->params[p].regoff;
		if (IS_INT_LNG_TYPE(t)) {                    /* integer args          */
			if (IS_2_WORD_TYPE(t))
				s2 = PACK_REGS(rd->argintregs[GET_LOW_REG(s1)],
							   rd->argintregs[GET_HIGH_REG(s1)]);
			else
				s2 = rd->argintregs[s1];
 			if (!md->params[p].inmemory) {           /* register arguments    */
 				if (!(var->flags & INMEMORY)) {      /* reg arg -> register   */
					M_NOP;
					if (IS_2_WORD_TYPE(t))		/* FIXME, only M_INTMOVE here */
						M_LNGMOVE(s2, var->regoff);
					else
						M_INTMOVE(s2, var->regoff);

				} else {                             /* reg arg -> spilled    */
					if (IS_2_WORD_TYPE(t))
						M_LST(s2, REG_SP, var->regoff * 4);
					else
						M_IST(s2, REG_SP, var->regoff * 4);
				}

			} else {                                 /* stack arguments       */
 				if (!(var->flags & INMEMORY)) {      /* stack arg -> register */
					if (IS_2_WORD_TYPE(t))
						M_LLD(var->regoff, REG_SP, (stackframesize + s1) * 4);
					else
						M_ILD(var->regoff, REG_SP, (stackframesize + s1) * 4);

				} else {                             /* stack arg -> spilled  */
#if 1
 					M_ILD(REG_ITMP1, REG_SP, (stackframesize + s1) * 4);
 					M_IST(REG_ITMP1, REG_SP, var->regoff * 4);
					if (IS_2_WORD_TYPE(t)) {
						M_ILD(REG_ITMP1, REG_SP, (stackframesize + s1) * 4 +4);
						M_IST(REG_ITMP1, REG_SP, var->regoff * 4 + 4);
					}
#else
					/* Reuse Memory Position on Caller Stack */
					var->regoff = stackframesize + s1;
#endif
				}
			}

		} else {                                     /* floating args         */
 			if (!md->params[p].inmemory) {           /* register arguments    */
				s2 = rd->argfltregs[s1];
 				if (!(var->flags & INMEMORY)) {      /* reg arg -> register   */
 					M_FLTMOVE(s2, var->regoff);

 				} else {			                 /* reg arg -> spilled    */
					if (IS_2_WORD_TYPE(t))
						M_DST(s2, REG_SP, var->regoff * 4);
					else
						M_FST(s2, REG_SP, var->regoff * 4);
 				}

 			} else {                                 /* stack arguments       */
 				if (!(var->flags & INMEMORY)) {      /* stack-arg -> register */
					if (IS_2_WORD_TYPE(t))
						M_DLD(var->regoff, REG_SP, (stackframesize + s1) * 4);

					else
						M_FLD(var->regoff, REG_SP, (stackframesize + s1) * 4);

 				} else {                             /* stack-arg -> spilled  */
#if 1
					if (IS_2_WORD_TYPE(t)) {
						M_DLD(REG_FTMP1, REG_SP, (stackframesize + s1) * 4);
						M_DST(REG_FTMP1, REG_SP, var->regoff * 4);
						var->regoff = stackframesize + s1;

					} else {
						M_FLD(REG_FTMP1, REG_SP, (stackframesize + s1) * 4);
						M_FST(REG_FTMP1, REG_SP, var->regoff * 4);
					}
#else
					/* Reuse Memory Position on Caller Stack */
					var->regoff = stackframesize + s1;
#endif
				}
			}
		}
	} /* end for */

	/* save monitorenter argument */

#if defined(ENABLE_THREADS)

	if (checksync && (m->flags & ACC_SYNCHRONIZED)) {

		/* stackoffset for argument used for LOCK_monitor_exit */
		s1 = rd->memuse;
#if !defined (NDEBUG)
		if (JITDATA_HAS_FLAG_VERBOSECALL(jd)) {
			M_AADD_IMM(REG_SP, -((LA_SIZE_IN_POINTERS + PA_SIZE_IN_POINTERS + ARG_CNT) * 8), REG_SP);

			for (p = 0; p < INT_ARG_CNT; p++)
				M_LST(rd->argintregs[p], REG_SP, LA_SIZE + PA_SIZE + p * 8);

			for (p = 0; p < FLT_ARG_CNT; p++)
				M_DST(rd->argfltregs[p], REG_SP, LA_SIZE + PA_SIZE + (INT_ARG_CNT + p) * 8);

			/* used for LOCK_monitor_exit, adopt size because we created another stackframe */
			s1 += (LA_SIZE_IN_POINTERS + PA_SIZE_IN_POINTERS + ARG_CNT);
		}
#endif
		p = dseg_addaddress(cd, LOCK_monitor_enter);
		M_ALD(REG_ITMP3, REG_PV, p);
		M_ALD(REG_ITMP3, REG_ITMP3, 0); /* TOC */
		M_MTCTR(REG_ITMP3);

		/* get or test the lock object */

		if (m->flags & ACC_STATIC) {
			p = dseg_addaddress(cd, &m->class->object.header);
			M_ALD(rd->argintregs[0], REG_PV, p);
		}
		else {
			M_TST(rd->argintregs[0]);
			M_BEQ(0);
			codegen_add_nullpointerexception_ref(cd);
		}

		M_AST(rd->argintregs[0], REG_SP, s1 * 8);	/* rd->memuse * 8 */
		M_JSR;
#if !defined (NDEBUG)
		if (JITDATA_HAS_FLAG_VERBOSECALL(jd)) {
			for (p = 0; p < INT_ARG_CNT; p++)
				M_LLD(rd->argintregs[p], REG_SP, LA_SIZE + PA_SIZE + p * 8);

			for (p = 0; p < FLT_ARG_CNT; p++)
				M_DLD(rd->argfltregs[p], REG_SP, LA_SIZE + PA_SIZE + (INT_ARG_CNT + p) * 8);

			M_AADD_IMM(REG_SP, (LA_SIZE_IN_POINTERS + PA_SIZE_IN_POINTERS + ARG_CNT) * 8, REG_SP);
		}
#endif
	}
#endif

	/* call trace function */
#if !defined (NDEBUG)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd))
		emit_verbosecall_enter(jd);

	}
#endif

	/* end of header generation */

	replacementpoint = jd->code->rplpoints;

	/* walk through all basic blocks */
	for (bptr = jd->new_basicblocks; bptr != NULL; bptr = bptr->next) {

		bptr->mpc = (s4) (cd->mcodeptr - cd->mcodebase);

		if (bptr->flags >= BBREACHED) {

		/* branch resolving */

		{
		branchref *brefs;
		for (brefs = bptr->branchrefs; brefs != NULL; brefs = brefs->next) {
			gen_resolvebranch((u1*) cd->mcodebase + brefs->branchpos, 
			                  brefs->branchpos,
							  bptr->mpc);
			}
		}

		/* handle replacement points */

#if 0
		if (bptr->bitflags & BBFLAG_REPLACEMENT) {
			replacementpoint->pc = (u1*)(ptrint)bptr->mpc; /* will be resolved later */
			
			replacementpoint++;
		}
#endif

		/* copy interface registers to their destination */

		len = bptr->indepth;
		MCODECHECK(64+len);

#if defined(ENABLE_LSRA)
		if (opt_lsra) {
			while (len) {
				len--;
				var = VAR(bptr->invars[len]);
				if ((len == bptr->indepth-1) && (bptr->type == BBTYPE_EXH)) {
					/* d = reg_of_var(m, var, REG_ITMP1); */
					if (!(var->flags & INMEMORY))
						d = var->regoff;
					else
						d = REG_ITMP1;
					M_INTMOVE(REG_ITMP1, d);
					emit_store(jd, NULL, var, d);
				}
			}
		} else {
#endif
		while (len) {
			len--;
			var = VAR(bptr->invars[len]);
			if ((len == bptr->indepth-1) && (bptr->type == BBTYPE_EXH)) {
				d = codegen_reg_of_var(0, var, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
				emit_store(jd, NULL, var, d);
			} 
			else {
				assert((var->flags & INOUT));
			}
		}

#if defined(ENABLE_LSRA)
		}
#endif
		/* walk through all instructions */
		
		len = bptr->icount;
		currentline = 0;
			
		for (iptr = bptr->iinstr; len > 0; len--, iptr++) {
			if (iptr->line != currentline) {
				dseg_addlinenumber(cd, iptr->line);
				currentline = iptr->line;
			}

			MCODECHECK(64);   /* an instruction usually needs < 64 words      */

			/* M_NOP; M_NOP; XXX */
			switch (iptr->opc) {
			case ICMD_NOP:    /* ...  ==> ...                                 */
			case ICMD_INLINE_START:
			case ICMD_INLINE_END:
				break;

		case ICMD_CHECKNULL:  /* ..., objectref  ==> ..., objectref           */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_TST(s1);
			M_BEQ(0);
			codegen_add_nullpointerexception_ref(cd);
			break;

		/* constant operations ************************************************/

		case ICMD_ICONST:     /* ...  ==> ..., constant                       */

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			ICONST(d, iptr->sx.val.i);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LCONST:     /* ...  ==> ..., constant                       */

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			LCONST(d, iptr->sx.val.l);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FCONST:     /* ...  ==> ..., constant                       */

			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			a = dseg_addfloat(cd, iptr->sx.val.f);
			M_FLD(d, REG_PV, a);
			emit_store_dst(jd, iptr, d);
			break;
			
		case ICMD_DCONST:     /* ...  ==> ..., constant                       */

			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			a = dseg_adddouble(cd, iptr->sx.val.d);
			M_DLD(d, REG_PV, a);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ACONST:     /* ...  ==> ..., constant                       */
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			disp = dseg_addaddress(cd, iptr->sx.val.anyptr);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_aconst,
									iptr->sx.val.c.ref,
								    disp);

				if (opt_showdisassemble)
					M_NOP;
			}

			M_ALD(d, REG_PV, disp);
			emit_store_dst(jd, iptr, d);
			break;


		/* load/store/copy/move operations ************************************/

		case ICMD_ILOAD:      /* ...  ==> ..., content of local variable      */
		case ICMD_ALOAD:      /* s1.localindex = local variable               */
		case ICMD_LLOAD:
		case ICMD_FLOAD:      /* ...  ==> ..., content of local variable      */
		case ICMD_DLOAD:      /* ...  ==> ..., content of local variable      */
		case ICMD_ISTORE:     /* ..., value  ==> ...                          */
		case ICMD_ASTORE:     /* dst.localindex = local variable              */
		case ICMD_LSTORE:
		case ICMD_FSTORE:     /* ..., value  ==> ...                          */
		case ICMD_DSTORE:     /* ..., value  ==> ...                          */
		case ICMD_COPY:
		case ICMD_MOVE:

			emit_copy(jd, iptr, VAROP(iptr->s1), VAROP(iptr->dst));
			break;


		/* pop operations *****************************************************/

		/* attention: double and longs are only one entry in CACAO ICMDs      */

		case ICMD_POP:        /* ..., value  ==> ...                          */
		case ICMD_POP2:       /* ..., value, value  ==> ...                   */

			break;


		/* integer operations *************************************************/

		case ICMD_INEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1); 
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_NEG(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_NEG(s1, d); /* XXX */
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_I2L:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_L2I:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_ISEXT(s1, d);	
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INT2BYTE:   /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_BSEXT(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INT2CHAR:   /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_CZEXT(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INT2SHORT:  /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_SSEXT(s1, d);
			emit_store_dst(jd, iptr, d);
			break;


		case ICMD_IADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_IADD(s1, s2, d);
			M_EXTSW(d,d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= -32768) && (iptr->sx.val.i <= 32767)) {
				M_IADD_IMM(s1, iptr->sx.val.i, d);
			} else {
				ICONST(REG_ITMP2, iptr->sx.val.i);
				M_IADD(s1, REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_LADD(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.l = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			/* XXX check me */
			if ((iptr->sx.val.l >= -32768) && (iptr->sx.val.l <= 32767)) {
				M_LADD_IMM(s1, iptr->sx.val.l, d);
			} else {
				LCONST(REG_ITMP2, iptr->sx.val.l);
				M_LADD(s1, REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_SUB(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISUBCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= -32767) && (iptr->sx.val.i <= 32768)) {
				M_IADD_IMM(s1, -iptr->sx.val.i, d);
			} else {
				ICONST(REG_ITMP2, iptr->sx.val.i);
				M_SUB(s1, REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_SUB(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LSUBCONST:  /* ..., value  ==> ..., value - constant        */
		                      /* sx.val.l = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			/* XXX check me */
			if ((iptr->sx.val.l >= -32768) && (iptr->sx.val.l <= 32767)) {
				M_LADD_IMM(s1, -iptr->sx.val.l, d);
			} else {
				LCONST(REG_ITMP2, iptr->sx.val.l);
				M_SUB(s1, REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IDIV:
		case ICMD_LDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_TST(s2);
			M_BEQ(0);
			codegen_add_arithmeticexception_ref(cd);

			M_DIV(s1, s2, d);

			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IREM:
		case ICMD_LREM:       /* ..., val1, val2  ==> ..., val1 % val2        */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_TST(s2);
			M_BEQ(0);
			codegen_add_arithmeticexception_ref(cd);

			/* FIXME s1 == -2^63 && s2 == -1 does not work that way */
			M_DIV(s1, s2, d);
			M_MUL( d, s2, d);
			M_SUB(s1,  d, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_MUL(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= -32768) && (iptr->sx.val.i <= 32767))
				M_MUL_IMM(s1, iptr->sx.val.i, d);
			else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				M_MUL(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IDIVPOW2:   /* ..., value  ==> ..., value << constant       */
		                      
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_SRA_IMM(s1, iptr->sx.val.i, d);
			M_ADDZE(d, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_AND_IMM(s2, 0x1f, REG_ITMP3);
			M_SLL(s1, REG_ITMP3, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHLCONST:  /* ..., value  ==> ..., value << constant       */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_SLL_IMM(s1, iptr->sx.val.i & 0x1f, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_AND_IMM(s2, 0x1f, REG_ITMP3);
			M_SRA(s1, REG_ITMP3, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_SRA_IMM(s1, iptr->sx.val.i & 0x1f, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_AND_IMM(s2, 0x1f, REG_ITMP2);
			M_SRL(s1, REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (iptr->sx.val.i & 0x1f) {
				M_SRL_IMM(s1, iptr->sx.val.i & 0x1f, d);
			} else {
				M_INTMOVE(s1, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IAND:       /* ..., val1, val2  ==> ..., val1 & val2        */
		case ICMD_LAND:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_AND(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= 0) && (iptr->sx.val.i <= 65535)) {
				M_AND_IMM(s1, iptr->sx.val.i, d);
				}
			/*
			else if (iptr->sx.val.i == 0xffffff) {
				M_RLWINM(s1, 0, 8, 31, d);
				}
			*/
			else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				M_AND(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* sx.val.l = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 65535))
				M_AND_IMM(s1, iptr->sx.val.l, d);
			/*
			else if (iptr->sx.val.l == 0xffffff) {
				M_RLWINM(s1, 0, 8, 31, d);
				}
			*/
			else {
				LCONST(REG_ITMP3, iptr->sx.val.l);
				M_AND(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_MOV(s1, REG_ITMP2);
			M_CMPI(s1, 0);
			M_BGE(1 + 2*(iptr->sx.val.i >= 32768));
			if (iptr->sx.val.i >= 32768) {
				M_ADDIS(REG_ZERO, iptr->sx.val.i >> 16, REG_ITMP2);
				M_OR_IMM(REG_ITMP2, iptr->sx.val.i, REG_ITMP2);
				M_IADD(s1, REG_ITMP2, REG_ITMP2);
			} else {
				M_IADD_IMM(s1, iptr->sx.val.i, REG_ITMP2);
			}
			{
				int b=0, m = iptr->sx.val.i;
				while (m >>= 1)
					++b;
				M_RLWINM(REG_ITMP2, 0, 0, 30-b, REG_ITMP2);
			}
			M_SUB(s1, REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IOR:        /* ..., val1, val2  ==> ..., val1 | val2        */
		case ICMD_LOR:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_OR(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= 0) && (iptr->sx.val.i <= 65535))
				M_OR_IMM(s1, iptr->sx.val.i, d);
			else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				M_OR(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.l = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 65535))
				M_OR_IMM(s1, iptr->sx.val.l, d);
			else {
				LCONST(REG_ITMP3, iptr->sx.val.l);
				M_OR(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;


		case ICMD_IXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */
		case ICMD_LXOR:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_XOR(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.i >= 0) && (iptr->sx.val.i <= 65535))
				M_XOR_IMM(s1, iptr->sx.val.i, d);
			else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				M_XOR(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* sx.val.l = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 65535))
				M_XOR_IMM(s1, iptr->sx.val.l, d);
			else {
				LCONST(REG_ITMP3, iptr->sx.val.l);
				M_XOR(s1, REG_ITMP3, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LCMP:       /* ..., val1, val2  ==> ..., val1 cmp val2      */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			/* XXX implement me!!! */
			emit_store_dst(jd, iptr, d);
			break;
			break;

		case ICMD_IINC:       /* ..., value  ==> ..., value + constant        */
		                      /* s1.localindex = variable, sx.val.i = constant*/

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);

			/* XXX implement me more efficiently */
			ICONST(REG_ITMP2, iptr->sx.val.i);
			M_IADD(s1, REG_ITMP2, d);

			emit_store_dst(jd, iptr, d);
			break;


		/* floating operations ************************************************/

		case ICMD_FNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FMOVN(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FMOVN(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FADD(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_DADD(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FSUB(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_DSUB(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FMUL(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_DMUL(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FDIV(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_DDIV(s1, s2, d);
			emit_store_dst(jd, iptr, d);
			break;
		
		case ICMD_F2I:       /* ..., value  ==> ..., (int) value              */
		case ICMD_D2I:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_CLR(d);
			disp = dseg_addfloat(cd, 0.0);
			M_FLD(REG_FTMP2, REG_PV, disp);
			M_FCMPU(s1, REG_FTMP2);
			M_BNAN(4);
			disp = dseg_adds4(cd, 0);
			M_CVTDL_C(s1, REG_FTMP1);
			M_LDA(REG_ITMP1, REG_PV, disp);
			M_STFIWX(REG_FTMP1, 0, REG_ITMP1);
			M_ILD(d, REG_PV, disp);
			emit_store_dst(jd, iptr, d);
			break;
		
		case ICMD_F2D:       /* ..., value  ==> ..., (double) value           */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_FLTMOVE(s1, d);
			emit_store_dst(jd, iptr, d);
			break;
					
		case ICMD_D2F:       /* ..., value  ==> ..., (double) value           */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_CVTDF(s1, d);
			emit_store_dst(jd, iptr, d);
			break;
		
		case ICMD_FCMPL:      /* ..., val1, val2  ==> ..., val1 fcmpg val2    */
		case ICMD_DCMPL:      /* == => 0, < => 1, > => -1                     */


			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_FCMPU(s2, s1);
			M_IADD_IMM(REG_ZERO, -1, d);
			M_BNAN(4);
			M_BGT(3);
			M_IADD_IMM(REG_ZERO, 0, d);
			M_BGE(1);
			M_IADD_IMM(REG_ZERO, 1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FCMPG:      /* ..., val1, val2  ==> ..., val1 fcmpl val2    */
		case ICMD_DCMPG:      /* == => 0, < => 1, > => -1                     */

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_FCMPU(s1, s2);
			M_IADD_IMM(REG_ZERO, 1, d);
			M_BNAN(4);
			M_BGT(3);
			M_IADD_IMM(REG_ZERO, 0, d);
			M_BGE(1);
			M_IADD_IMM(REG_ZERO, -1, d);
			emit_store_dst(jd, iptr, d);
			break;
			
		case ICMD_IF_FCMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPEQ:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(1);
			M_BEQ(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPNE:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(0);
			codegen_addreference(cd, iptr->dst.block);
			M_BNE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;


		case ICMD_IF_FCMPL_LT:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPL_LT:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(0);
			codegen_addreference(cd, iptr->dst.block);
			M_BLT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPL_GT:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPL_GT:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(1);
			M_BGT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPL_LE:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPL_LE:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(0);
			codegen_addreference(cd, iptr->dst.block);
			M_BLE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPL_GE:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPL_GE:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(1);
			M_BGE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPG_LT:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPG_LT:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(1);
			M_BLT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPG_GT:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPG_GT:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(0);
			codegen_addreference(cd, iptr->dst.block);
			M_BGT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPG_LE:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPG_LE:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(1);
			M_BLE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_FCMPG_GE:  /* ..., value, value ==> ...                  */
		case ICMD_IF_DCMPG_GE:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			M_FCMPU(s1, s2);
			M_BNAN(0);
			codegen_addreference(cd, iptr->dst.block);
			M_BGE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;


		/* memory operations **************************************************/

		case ICMD_ARRAYLENGTH: /* ..., arrayref  ==> ..., length              */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			gen_nullptr_check(s1);
			M_ILD(d, s1, OFFSET(java_arrayheader, size));
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_BALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_IADD_IMM(s2, OFFSET(java_chararray, data[0]), REG_ITMP2);
			M_LBZX(d, s1, REG_ITMP2);
			M_BSEXT(d, d);
			emit_store_dst(jd, iptr, d);
			break;			

		case ICMD_CALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 1, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_chararray, data[0]), REG_ITMP2);
			M_LHAX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_SALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 1, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_shortarray, data[0]), REG_ITMP2);
			M_LHAX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 2, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_intarray, data[0]), REG_ITMP2);
			M_LWZX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, PACK_REGS(REG_ITMP2, REG_ITMP1));
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD(s1, REG_ITMP2, REG_ITMP2);
			M_LLD_INTERN(d, REG_ITMP2, OFFSET(java_longarray, data[0]));
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 2, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_floatarray, data[0]), REG_ITMP2);
			M_LFSX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_doublearray, data[0]), REG_ITMP2);
			M_LFDX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_AALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_objectarray, data[0]), REG_ITMP2);
			M_ALDX(d, s1, REG_ITMP2);
			emit_store_dst(jd, iptr, d);
			break;


		case ICMD_BASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_IADD_IMM(s2, OFFSET(java_bytearray, data[0]), REG_ITMP2);
			M_STBX(s3, s1, REG_ITMP2);
			break;

		case ICMD_CASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_SLL_IMM(s2, 1, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_chararray, data[0]), REG_ITMP2);
			M_STHX(s3, s1, REG_ITMP2);
			break;

		case ICMD_SASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_SLL_IMM(s2, 1, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_shortarray, data[0]), REG_ITMP2);
			M_STHX(s3, s1, REG_ITMP2);
			break;

		case ICMD_IASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_SLL_IMM(s2, 2, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_intarray, data[0]), REG_ITMP2);
			M_STWX(s3, s1, REG_ITMP2);
			break;

		case ICMD_LASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_longarray, data[0]), REG_ITMP2);
			M_LST(s3, s1, REG_ITMP2);
			break;

		case ICMD_FASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_FTMP3);
			M_SLL_IMM(s2, 2, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_floatarray, data[0]), REG_ITMP2);
			M_STFSX(s3, s1, REG_ITMP2);
			break;

		case ICMD_DASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, REG_FTMP3);
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_doublearray, data[0]), REG_ITMP2);
			M_STFDX(s3, s1, REG_ITMP2);
			break;

		case ICMD_AASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, rd->argintregs[0]);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, rd->argintregs[1]);

			disp = dseg_addaddress(cd, BUILTIN_canstore);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_ALD(REG_ITMP3, REG_ITMP3, 0); /* TOC */
			M_MTCTR(REG_ITMP3);

			M_INTMOVE(s1, rd->argintregs[0]);
			M_INTMOVE(s3, rd->argintregs[1]);

			M_JSR;
			M_TST(REG_RESULT);
			M_BEQ(0);
			codegen_add_arraystoreexception_ref(cd);

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_SLL_IMM(s2, 3, REG_ITMP2);
			M_IADD_IMM(REG_ITMP2, OFFSET(java_objectarray, data[0]), REG_ITMP2);
			M_ASTX(s3, s1, REG_ITMP2);
			break;


		case ICMD_GETSTATIC:  /* ...  ==> ..., value                          */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				unresolved_field *uf = iptr->sx.s23.s3.uf;

				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp = dseg_addaddress(cd, NULL);

				codegen_addpatchref(cd, PATCHER_get_putstatic,
									iptr->sx.s23.s3.uf, disp);

				if (opt_showdisassemble)
					M_NOP;

			} else {
				fieldinfo *fi = iptr->sx.s23.s3.fmiref->p.field;

				fieldtype = fi->type;
				disp = dseg_addaddress(cd, &(fi->value));

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					codegen_addpatchref(cd, PATCHER_clinit, fi->class, disp);

					if (opt_showdisassemble)
						M_NOP;
				}
  			}

			M_ALD(REG_ITMP1, REG_PV, disp);
			switch (fieldtype) {
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ILD_INTERN(d, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				d = codegen_reg_of_dst(jd, iptr, PACK_REGS(REG_ITMP2, REG_ITMP1));
				M_ILD_INTERN(GET_LOW_REG(d), REG_ITMP1, 4);/* keep this order */
				M_ILD_INTERN(GET_HIGH_REG(d), REG_ITMP1, 0);/*keep this order */
				break;
			case TYPE_ADR:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ALD_INTERN(d, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_FLD_INTERN(d, REG_ITMP1, 0);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_DLD_INTERN(d, REG_ITMP1, 0);
				break;
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_PUTSTATIC:  /* ..., value  ==> ...                          */


			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				unresolved_field *uf = iptr->sx.s23.s3.uf;

				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp = dseg_addaddress(cd, NULL);

				codegen_addpatchref(cd, PATCHER_get_putstatic,
									iptr->sx.s23.s3.uf, disp);

				if (opt_showdisassemble)
					M_NOP;

			} else {
				fieldinfo *fi = iptr->sx.s23.s3.fmiref->p.field;

				fieldtype = fi->type;
				disp = dseg_addaddress(cd, &(fi->value));

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					codegen_addpatchref(cd, PATCHER_clinit, fi->class, disp);

					if (opt_showdisassemble)
						M_NOP;
				}
  			}

			M_ALD(REG_ITMP1, REG_PV, disp);
			switch (fieldtype) {
			case TYPE_INT:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_IST_INTERN(s1, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_LST_INTERN(s1, REG_ITMP1, 0);
				break;
			case TYPE_ADR:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_AST_INTERN(s1, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_FST_INTERN(s1, REG_ITMP1, 0);
				break;
			case TYPE_DBL:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_DST_INTERN(s1, REG_ITMP1, 0);
				break;
			}
			break;


		case ICMD_GETFIELD:   /* ...  ==> ..., value                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			gen_nullptr_check(s1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				unresolved_field *uf = iptr->sx.s23.s3.uf;

				fieldtype = uf->fieldref->parseddesc.fd->type;

				codegen_addpatchref(cd, PATCHER_get_putfield,
									iptr->sx.s23.s3.uf, 0);

				if (opt_showdisassemble)
					M_NOP;

				disp = 0;

			} else {
				fieldinfo *fi = iptr->sx.s23.s3.fmiref->p.field;

				fieldtype = fi->type;
				disp = fi->offset;
			}

			switch (fieldtype) {
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ILD(d, s1, disp);
				break;
			case TYPE_LNG:
   				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_LLD(d, s1, disp);
				break;
			case TYPE_ADR:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ALD(d, s1, disp);
				break;
			case TYPE_FLT:
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_FLD(d, s1, disp);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_DLD(d, s1, disp);
				break;
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_PUTFIELD:   /* ..., value  ==> ...                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			gen_nullptr_check(s1);

			if (!IS_FLT_DBL_TYPE(fieldtype)) {
				s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			} else {
				s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			}

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				unresolved_field *uf = iptr->sx.s23.s3.uf;

				fieldtype = uf->fieldref->parseddesc.fd->type;

				codegen_addpatchref(cd, PATCHER_get_putfield,
									iptr->sx.s23.s3.uf, 0);

				if (opt_showdisassemble)
					M_NOP;

				disp = 0;

			} else {
				fieldinfo *fi = iptr->sx.s23.s3.fmiref->p.field;

				fieldtype = fi->type;
				disp = fi->offset;
			}

			switch (fieldtype) {
			case TYPE_INT:
				M_IST(s2, s1, disp);
				break;
			case TYPE_LNG:
				M_LST(s2, s1, disp);
				break;
			case TYPE_ADR:
				M_AST(s2, s1, disp);
				break;
			case TYPE_FLT:
				M_FST(s2, s1, disp);
				break;
			case TYPE_DBL:
				M_DST(s2, s1, disp);
				break;
			}
			break;


		/* branch operations **************************************************/

		case ICMD_ATHROW:       /* ..., objectref ==> ... (, objectref)       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_LNGMOVE(s1, REG_ITMP1_XPTR);

#ifdef ENABLE_VERIFIER
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_athrow_areturn,
									iptr->sx.s23.s2.uc, 0);

				if (opt_showdisassemble)
					M_NOP;
			}
#endif /* ENABLE_VERIFIER */

			disp = dseg_addaddress(cd, asm_handle_exception);
			M_ALD(REG_ITMP2, REG_PV, disp);
			M_MTCTR(REG_ITMP2);

			if (jd->isleafmethod) M_MFLR(REG_ITMP3);         /* save LR        */
			M_BL(0);                                        /* get current PC */
			M_MFLR(REG_ITMP2_XPC);
			if (jd->isleafmethod) M_MTLR(REG_ITMP3);         /* restore LR     */
			M_RTS;                                          /* jump to CTR    */

			ALIGNCODENOP;
			break;

		case ICMD_GOTO:         /* ... ==> ...                                */
		case ICMD_RET:          /* ... ==> ...                                */

			M_BR(0);
			codegen_addreference(cd, iptr->dst.block);
			ALIGNCODENOP;
			break;

		case ICMD_JSR:          /* ... ==> ...                                */

			M_BR(0);
			codegen_addreference(cd, iptr->sx.s23.s3.jsrtarget.block);
			ALIGNCODENOP;
			break;

		case ICMD_IFNULL:       /* ..., value ==> ...                         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_TST(s1);
			M_BEQ(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IFNONNULL:    /* ..., value ==> ...                         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_TST(s1);
			M_BNE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IFLT:
		case ICMD_IFLE:
		case ICMD_IFNE:
		case ICMD_IFGT:
		case ICMD_IFGE:
		case ICMD_IFEQ:         /* ..., value ==> ...                         */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			if ((iptr->sx.val.i >= -32768) && (iptr->sx.val.i <= 32767))
				M_CMPI(s1, iptr->sx.val.i);
			else {
				ICONST(REG_ITMP2, iptr->sx.val.i);
				M_CMP(s1, REG_ITMP2);
			}
			switch (iptr->opc) {
			case ICMD_IFLT:
				M_BLT(0);
				break;
			case ICMD_IFLE:
				M_BLE(0);
				break;
			case ICMD_IFNE:
				M_BNE(0);
				break;
			case ICMD_IFGT:
				M_BGT(0);
				break;
			case ICMD_IFGE:
				M_BGE(0);
				break;
			case ICMD_IFEQ:
				M_BEQ(0);
				break;
			}
			codegen_addreference(cd, iptr->dst.block);
			break;

		#if 0
		case ICMD_IF_LEQ:       /* ..., value ==> ...                         */

			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
			if (iptr->sx.val.l == 0) {
				M_OR_TST(s1, s2, REG_ITMP3);
  			} else if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
				M_XOR_IMM(s2, 0, REG_ITMP2);
				M_XOR_IMM(s1, iptr->sx.val.l & 0xffff, REG_ITMP1);
				M_OR_TST(REG_ITMP1, REG_ITMP2, REG_ITMP3);
  			} else {
				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_XOR(s1, REG_ITMP3, REG_ITMP1);
				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
				M_XOR(s2, REG_ITMP3, REG_ITMP2);
				M_OR_TST(REG_ITMP1, REG_ITMP2, REG_ITMP3);
			}
			M_BEQ(0);
			codegen_addreference(cd, iptr->dst.block);
			break;
			
		case ICMD_IF_LLT:       /* ..., value ==> ...                         */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
			if (iptr->sx.val.l == 0) {
				/* if high word is less than zero, the whole long is too */
				M_CMPI(s2, 0);
			} else if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
  				M_CMPI(s2, 0);
				M_BLT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BGT(2);
  				M_CMPUI(s1, iptr->sx.val.l & 0xffff);
  			} else {
  				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
  				M_CMP(s2, REG_ITMP3);
				M_BLT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BGT(3);
  				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_CMPU(s1, REG_ITMP3);
			}
			M_BLT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;
			
		case ICMD_IF_LLE:       /* ..., value ==> ...                         */

			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
/*  			if (iptr->sx.val.l == 0) { */
/*  				M_OR(s1, s2, REG_ITMP3); */
/*  				M_CMPI(REG_ITMP3, 0); */

/*    			} else  */
			if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
  				M_CMPI(s2, 0);
				M_BLT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BGT(2);
  				M_CMPUI(s1, iptr->sx.val.l & 0xffff);
  			} else {
  				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
  				M_CMP(s2, REG_ITMP3);
				M_BLT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BGT(3);
  				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_CMPU(s1, REG_ITMP3);
			}
			M_BLE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_LNE:       /* ..., value ==> ...                         */

			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
			if (iptr->sx.val.l == 0) {
				M_OR_TST(s1, s2, REG_ITMP3);
			} else if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
				M_XOR_IMM(s2, 0, REG_ITMP2);
				M_XOR_IMM(s1, iptr->sx.val.l & 0xffff, REG_ITMP1);
				M_OR_TST(REG_ITMP1, REG_ITMP2, REG_ITMP3);
  			} else {
				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_XOR(s1, REG_ITMP3, REG_ITMP1);
				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
				M_XOR(s2, REG_ITMP3, REG_ITMP2);
				M_OR_TST(REG_ITMP1, REG_ITMP2, REG_ITMP3);
			}
			M_BNE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;
			
		case ICMD_IF_LGT:       /* ..., value ==> ...                         */

			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
/*  			if (iptr->sx.val.l == 0) { */
/*  				M_OR(s1, s2, REG_ITMP3); */
/*  				M_CMPI(REG_ITMP3, 0); */

/*    			} else  */
			if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
  				M_CMPI(s2, 0);
				M_BGT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BLT(2);
  				M_CMPUI(s1, iptr->sx.val.l & 0xffff);
  			} else {
  				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
  				M_CMP(s2, REG_ITMP3);
				M_BGT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BLT(3);
  				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_CMPU(s1, REG_ITMP3);
			}
			M_BGT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;
			
		case ICMD_IF_LGE:       /* ..., value ==> ...                         */

			/* TODO, remove me */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
			if (iptr->sx.val.l == 0) {
				/* if high word is greater equal zero, the whole long is too */
				M_CMPI(s2, 0);
			} else if ((iptr->sx.val.l >= 0) && (iptr->sx.val.l <= 0xffff)) {
  				M_CMPI(s2, 0);
				M_BGT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BLT(2);
  				M_CMPUI(s1, iptr->sx.val.l & 0xffff);
  			} else {
  				ICONST(REG_ITMP3, iptr->sx.val.l >> 32);
  				M_CMP(s2, REG_ITMP3);
				M_BGT(0);
				codegen_addreference(cd, iptr->dst.block);
				M_BLT(3);
  				ICONST(REG_ITMP3, iptr->sx.val.l & 0xffffffff);
				M_CMPU(s1, REG_ITMP3);
			}
			M_BGE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;
		#endif

		case ICMD_IF_ICMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPEQ:    /* op1 = target JavaVM pc                     */
		case ICMD_IF_LCMPEQ: 

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BEQ(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_ICMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPNE:    /* op1 = target JavaVM pc                     */
		case ICMD_IF_LCMPNE:  

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BNE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;


		case ICMD_IF_ICMPLT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPLT:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BLT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_ICMPGT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPGT:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BGT(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_ICMPLE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPLE:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BLE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;

		case ICMD_IF_ICMPGE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPGE:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			M_BGE(0);
			codegen_addreference(cd, iptr->dst.block);
			break;


		case ICMD_LRETURN:      /* ..., retvalue ==> ...                      */
		case ICMD_IRETURN:      /* ..., retvalue ==> ...                      */

			s1 = emit_load_s1(jd, iptr, REG_RESULT);
			M_LNGMOVE(s1, REG_RESULT);
			goto nowperformreturn;

		case ICMD_ARETURN:      /* ..., retvalue ==> ...                      */

			s1 = emit_load_s1(jd, iptr, REG_RESULT);
			M_LNGMOVE(s1, REG_RESULT);

#ifdef ENABLE_VERIFIER
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_athrow_areturn,
									iptr->sx.s23.s2.uc, 0);

				if (opt_showdisassemble)
					M_NOP;
			}
#endif /* ENABLE_VERIFIER */

			goto nowperformreturn;

		case ICMD_FRETURN:      /* ..., retvalue ==> ...                      */
		case ICMD_DRETURN:

			s1 = emit_load_s1(jd, iptr, REG_FRESULT);
			M_FLTMOVE(s1, REG_FRESULT);
			goto nowperformreturn;

		case ICMD_RETURN:      /* ...  ==> ...                                */

nowperformreturn:
			{
			s4 i, p;
			
			p = stackframesize;

			/* call trace function */

#if !defined(NDEBUG)
			if (JITDATA_HAS_FLAG_VERBOSECALL(jd)) {
				emit_verbosecall_exit(jd);
			}
#endif		

#if defined(ENABLE_THREADS)
			if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
				disp = dseg_addaddress(cd, LOCK_monitor_exit);
				M_ALD(REG_ITMP3, REG_PV, disp);
				M_ALD(REG_ITMP3, REG_ITMP3, 0); /* TOC */
				M_MTCTR(REG_ITMP3);

				/* we need to save the proper return value */

				switch (iptr->opc) {
				case ICMD_LRETURN:
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					/* fall through */
					M_LST(REG_RESULT , REG_SP, rd->memuse * 8 + 8);
					break;
				case ICMD_FRETURN:
					M_FST(REG_FRESULT, REG_SP, rd->memuse * 8 + 8);
					break;
				case ICMD_DRETURN:
					M_DST(REG_FRESULT, REG_SP, rd->memuse * 8 + 8);
					break;
				}

				M_ALD(rd->argintregs[0], REG_SP, rd->memuse * 8);
				M_JSR;

				/* and now restore the proper return value */

				switch (iptr->opc) {
				case ICMD_LRETURN:
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					/* fall through */
					M_LLD(REG_RESULT , REG_SP, rd->memuse * 8 + 8);
					break;
				case ICMD_FRETURN:
					M_FLD(REG_FRESULT, REG_SP, rd->memuse * 8 + 8);
					break;
				case ICMD_DRETURN:
					M_DLD(REG_FRESULT, REG_SP, rd->memuse * 8 + 8);
					break;
				}
			}
#endif

			/* restore return address                                         */

			if (!jd->isleafmethod) {
				/* ATTENTION: Don't use REG_ZERO (r0) here, as M_ALD
				   may have a displacement overflow. */

				M_ALD(REG_ITMP1, REG_SP, p * 8 + LA_LR_OFFSET);
				M_MTLR(REG_ITMP1);
			}

			/* restore saved registers                                        */

			for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
				p--; M_LLD(rd->savintregs[i], REG_SP, p * 8);
			}
			for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
				p--; M_DLD(rd->savfltregs[i], REG_SP, p * 8);
			}

			/* deallocate stack                                               */

			if (stackframesize)
				M_LDA(REG_SP, REG_SP, stackframesize * 8);

			M_RET;
			ALIGNCODENOP;
			}
			break;


		case ICMD_TABLESWITCH:  /* ..., index ==> ...                         */
			{
			s4 i, l;
			branch_target_t *table;

			table = iptr->dst.table;

			l = iptr->sx.s23.s2.tablelow;
			i = iptr->sx.s23.s3.tablehigh;

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			if (l == 0) {
				M_INTMOVE(s1, REG_ITMP1);
			} else if (l <= 32768) {
				M_LDA(REG_ITMP1, s1, -l);
			} else {
				ICONST(REG_ITMP2, l);
				M_SUB(s1, REG_ITMP2, REG_ITMP1);
			}

			/* number of targets */
			i = i - l + 1;

			/* range check */

			M_CMPUI(REG_ITMP1, i - 1);
			M_BGT(0);
			codegen_addreference(cd, table[0].block);

			/* build jump table top down and use address of lowest entry */

			table += i;

			while (--i >= 0) {
				dseg_addtarget(cd, table->block); 
				--table;
				}
			}

			/* length of dataseg after last dseg_addtarget is used by load */

			M_SLL_IMM(REG_ITMP1, 2, REG_ITMP1);
			M_IADD(REG_ITMP1, REG_PV, REG_ITMP2);
			M_ALD(REG_ITMP2, REG_ITMP2, -(cd->dseglen));
			M_MTCTR(REG_ITMP2);
			M_RTS;
			ALIGNCODENOP;
			break;


		case ICMD_LOOKUPSWITCH: /* ..., key ==> ...                           */
			{
			s4 i, val;
			lookup_target_t *lookup;

			lookup = iptr->dst.lookup;

			i = iptr->sx.s23.s2.lookupcount;
			
			MCODECHECK((i<<2)+8);
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			while (--i >= 0) {
				val = lookup->value;
				if ((val >= -32768) && (val <= 32767)) {
					M_CMPI(s1, val);
				} else {
					a = dseg_adds4(cd, val);
					M_ILD(REG_ITMP2, REG_PV, a);
					M_CMP(s1, REG_ITMP2);
				}
				M_BEQ(0);
				codegen_addreference(cd, lookup->target.block);
				++lookup;
			}

			M_BR(0);
			codegen_addreference(cd, iptr->sx.s23.s3.lookupdefault.block);

			ALIGNCODENOP;
			break;
			}


		case ICMD_BUILTIN:      /* ..., [arg1, [arg2 ...]] ==> ...            */

			bte = iptr->sx.s23.s3.bte;
			md = bte->md;
			goto gen_method;

		case ICMD_INVOKESTATIC: /* ..., [arg1, [arg2 ...]] ==> ...            */

		case ICMD_INVOKESPECIAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
		case ICMD_INVOKEVIRTUAL:/* op1 = arg count, val.a = method pointer    */
		case ICMD_INVOKEINTERFACE:

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				md = iptr->sx.s23.s3.um->methodref->parseddesc.md;
				lm = NULL;
			}
			else {
				lm = iptr->sx.s23.s3.fmiref->p.method;
				md = lm->parseddesc;
			}

gen_method:
			s3 = md->paramcount;

			MCODECHECK((s3 << 1) + 64);

			/* copy arguments to registers or stack location */

			for (s3 = s3 - 1; s3 >= 0; s3--) {
				var = VAR(iptr->sx.s23.s2.args[s3]);

				if (var->flags & PREALLOC)
					continue;

				if (IS_INT_LNG_TYPE(var->type)) {
					if (!md->params[s3].inmemory) {
						s1 = rd->argintregs[md->params[s3].regoff];
						d = emit_load(jd, iptr, var, s1);
						M_LNGMOVE(d, s1);
					} else {
						d = emit_load(jd, iptr, var, REG_ITMP1);
						M_LST(d, REG_SP, md->params[s3].regoff * 8);
					}
				} else {
					if (!md->params[s3].inmemory) {
						s1 = rd->argfltregs[md->params[s3].regoff];
						d = emit_load(jd, iptr, var, s1);
						M_FLTMOVE(d, s1);
					} else {
						d = emit_load(jd, iptr, var, REG_FTMP1);
						if (IS_2_WORD_TYPE(var->type))
							M_DST(d, REG_SP, md->params[s3].regoff * 8);
						else
							M_FST(d, REG_SP, md->params[s3].regoff * 8);
					}
				}
			} /* end of for */

			switch (iptr->opc) {
			case ICMD_BUILTIN:
				disp = dseg_addaddress(cd, bte->fp);
				d = md->returntype.type;

				M_ALD(REG_PV, REG_PV, disp);  	/* pointer to built-in-function descriptor */
				M_ALD(REG_ITMP1, REG_PV, 0);	/* function entry point address, what about TOC */
				M_MTCTR(REG_ITMP1);
				M_JSR;

				disp = (s4) (cd->mcodeptr - cd->mcodebase);
				M_MFLR(REG_ITMP1);
				M_LDA(REG_PV, REG_ITMP1, -disp);


				if (INSTRUCTION_MUST_CHECK(iptr)) {
					M_CMPI(REG_RESULT, 0);
					M_BEQ(0);
					codegen_add_fillinstacktrace_ref(cd);
				}
				break;

			case ICMD_INVOKESPECIAL:
				gen_nullptr_check(rd->argintregs[0]);
				M_ILD(REG_ITMP1, rd->argintregs[0], 0); /* hardware nullptr   */
				/* fall through */

			case ICMD_INVOKESTATIC:
				if (lm == NULL) {
					unresolved_method *um = iptr->sx.s23.s3.um;

					disp = dseg_addaddress(cd, NULL);

					codegen_addpatchref(cd, PATCHER_invokestatic_special,
										um, disp);

					if (opt_showdisassemble)
						M_NOP;

					d = md->returntype.type;

				} else {
					disp = dseg_addaddress(cd, lm->stubroutine);
					d = md->returntype.type;
				}

				M_NOP;
				M_ALD(REG_PV, REG_PV, disp);
				M_MTCTR(REG_PV);
				M_JSR;
				disp = (s4) (cd->mcodeptr - cd->mcodebase);
				M_MFLR(REG_ITMP1);
				M_LDA(REG_PV, REG_ITMP1, -disp);
				break;

			case ICMD_INVOKEVIRTUAL:
				gen_nullptr_check(rd->argintregs[0]);

				if (lm == NULL) {
					unresolved_method *um = iptr->sx.s23.s3.um;

					codegen_addpatchref(cd, PATCHER_invokevirtual, um, 0);

					if (opt_showdisassemble)
						M_NOP;

					s1 = 0;
					d = md->returntype.type;

				} else {
					s1 = OFFSET(vftbl_t, table[0]) +
						sizeof(methodptr) * lm->vftblindex;
					d = md->returntype.type;
				}

				M_ALD(REG_METHODPTR, rd->argintregs[0],
					  OFFSET(java_objectheader, vftbl));
				M_ALD(REG_PV, REG_METHODPTR, s1);
				M_MTCTR(REG_PV);
				M_JSR;
				disp = (s4) (cd->mcodeptr - cd->mcodebase);
				M_MFLR(REG_ITMP1);
				M_LDA(REG_PV, REG_ITMP1, -disp);
				break;

			case ICMD_INVOKEINTERFACE:
				gen_nullptr_check(rd->argintregs[0]);

				if (lm == NULL) {
					unresolved_method *um = iptr->sx.s23.s3.um;

					codegen_addpatchref(cd, PATCHER_invokeinterface, um, 0);

					if (opt_showdisassemble)
						M_NOP;

					s1 = 0;
					s2 = 0;
					d = md->returntype.type;

				} else {
					s1 = OFFSET(vftbl_t, interfacetable[0]) -
						sizeof(methodptr*) * lm->class->index;

					s2 = sizeof(methodptr) * (lm - lm->class->methods);

					d = md->returntype.type;
				}

				M_ALD(REG_METHODPTR, rd->argintregs[0],
					  OFFSET(java_objectheader, vftbl));    
				M_ALD(REG_METHODPTR, REG_METHODPTR, s1);
				M_ALD(REG_PV, REG_METHODPTR, s2);
				M_MTCTR(REG_PV);
				M_JSR;
				disp = (s4) (cd->mcodeptr - cd->mcodebase);
				M_MFLR(REG_ITMP1);
				M_LDA(REG_PV, REG_ITMP1, -disp);
				break;
			}

			/* d contains return type */

			if (d != TYPE_VOID) {
				if (IS_INT_LNG_TYPE(d)) {
					s1 = codegen_reg_of_dst(jd, iptr, REG_RESULT);
					M_MOV(REG_RESULT, s1);
				} else {
					s1 = codegen_reg_of_dst(jd, iptr, REG_FRESULT);
					M_FLTMOVE(REG_FRESULT, s1);
				}
				emit_store_dst(jd, iptr, s1);
			}
			break;


		case ICMD_CHECKCAST:  /* ..., objectref ==> ..., objectref            */
		                      /* val.a: (classinfo*) superclass               */

			/*  superclass is an interface:
			 *
			 *  OK if ((sub == NULL) ||
			 *         (sub->vftbl->interfacetablelength > super->index) &&
			 *         (sub->vftbl->interfacetable[-super->index] != NULL));
			 *
			 *  superclass is a class:
			 *
			 *  OK if ((sub == NULL) || (0
			 *         <= (sub->vftbl->baseval - super->vftbl->baseval) <=
			 *         super->vftbl->diffvall));
			 */

			if (!(iptr->flags.bits & INS_FLAG_ARRAY)) {
				/* object type cast-check */

				classinfo *super;
				vftbl_t   *supervftbl;
				s4         superindex;

				if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
					super = NULL;
					superindex = 0;
					supervftbl = NULL;
				}
				else {
					super = iptr->sx.s23.s3.c.cls;
					superindex = super->index;
					supervftbl = super->vftbl;
				}
			
#if defined(ENABLE_THREADS)
				codegen_threadcritrestart(cd, cd->mcodeptr - cd->mcodebase);
#endif
				s1 = emit_load_s1(jd, iptr, REG_ITMP1);

				/* calculate interface checkcast code size */

				s2 = 7;
				if (!super)
					s2 += (opt_showdisassemble ? 1 : 0);

				/* calculate class checkcast code size */

				s3 = 8 + (s1 == REG_ITMP1);
				if (!super)
					s3 += (opt_showdisassemble ? 1 : 0);

				/* if class is not resolved, check which code to call */

				if (!super) {
					M_TST(s1);
					M_BEQ(3 + (opt_showdisassemble ? 1 : 0) + s2 + 1 + s3);

					disp = dseg_adds4(cd, 0);                     /* super->flags */

					codegen_addpatchref(cd,
										PATCHER_checkcast_instanceof_flags,
										iptr->sx.s23.s3.c.ref,
										disp);

					if (opt_showdisassemble)
						M_NOP;

					M_ILD(REG_ITMP2, REG_PV, disp);
					M_AND_IMM(REG_ITMP2, ACC_INTERFACE, REG_ITMP2);
					M_BEQ(s2 + 1);
				}

				/* interface checkcast code */

				if (!super || (super->flags & ACC_INTERFACE)) {
					if (super) {
						M_TST(s1);
						M_BEQ(s2);

					} else {
						codegen_addpatchref(cd,
											PATCHER_checkcast_instanceof_interface,
											iptr->sx.s23.s3.c.ref,
											0);

						if (opt_showdisassemble)
							M_NOP;
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));
					M_ILD(REG_ITMP3, REG_ITMP2, OFFSET(vftbl_t, interfacetablelength));
					M_LDATST(REG_ITMP3, REG_ITMP3, -superindex);
					M_BLE(0);
					codegen_add_classcastexception_ref(cd, s1);	/*XXX s1?? */
					M_ALD(REG_ITMP3, REG_ITMP2,
						  OFFSET(vftbl_t, interfacetable[0]) -
						  superindex * sizeof(methodptr*));
					M_TST(REG_ITMP3);
					M_BEQ(0);
					codegen_add_classcastexception_ref(cd, s1);	/*XXX s1??*/

					if (!super)
						M_BR(s3);
				}

				/* class checkcast code */

				if (!super || !(super->flags & ACC_INTERFACE)) {
					disp = dseg_addaddress(cd, supervftbl);

					if (super) {
						M_TST(s1);
						M_BEQ(s3);

					} else {
						codegen_addpatchref(cd, PATCHER_checkcast_class,
											iptr->sx.s23.s3.c.ref,
											disp);

						if (opt_showdisassemble)
							M_NOP;
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));
#if defined(ENABLE_THREADS)
					codegen_threadcritstart(cd, cd->mcodeptr - cd->mcodebase);
#endif
					M_ILD(REG_ITMP3, REG_ITMP2, OFFSET(vftbl_t, baseval));
					M_ALD(REG_ITMP2, REG_PV, disp);
					if (s1 != REG_ITMP1) {
						M_ILD(REG_ITMP1, REG_ITMP2, OFFSET(vftbl_t, baseval));
						M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, diffval));
#if defined(ENABLE_THREADS)
						codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
						M_SUB(REG_ITMP3, REG_ITMP1, REG_ITMP3);
					} else {
						M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, baseval));
						M_SUB(REG_ITMP3, REG_ITMP2, REG_ITMP3);
						M_ALD(REG_ITMP2, REG_PV, disp);
						M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, diffval));
#if defined(ENABLE_THREADS)
						codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
					}
					M_CMP(REG_ITMP3, REG_ITMP2);
					M_BGT(0);
					codegen_add_classcastexception_ref(cd, s1); /* XXX s1? */
				}
				d = codegen_reg_of_dst(jd, iptr, s1);

			} else {
				/* array type cast-check */

				s1 = emit_load_s1(jd, iptr, rd->argintregs[0]);
				M_INTMOVE(s1, rd->argintregs[0]);

				disp = dseg_addaddress(cd, iptr->sx.s23.s3.c.cls);

				if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
					codegen_addpatchref(cd, PATCHER_builtin_arraycheckcast,
										iptr->sx.s23.s3.c.ref,
										disp);

					if (opt_showdisassemble)
						M_NOP;
				}

				M_ALD(rd->argintregs[1], REG_PV, disp);
				disp = dseg_addaddress(cd, BUILTIN_arraycheckcast);
				M_ALD(REG_ITMP2, REG_PV, disp);
				M_ALD(REG_ITMP2, REG_ITMP2, 0);	/* TOC */
				M_MTCTR(REG_ITMP2);
				M_JSR;
				M_TST(REG_RESULT);
				M_BEQ(0);
				codegen_add_classcastexception_ref(cd, s1); /* XXX s1? */

				s1 = emit_load_s1(jd, iptr, REG_ITMP1);
				d = codegen_reg_of_dst(jd, iptr, s1);
			}
			M_INTMOVE(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INSTANCEOF: /* ..., objectref ==> ..., intresult            */
		                      /* val.a: (classinfo*) superclass               */

			/*  superclass is an interface:
			 *
			 *  return (sub != NULL) &&
			 *         (sub->vftbl->interfacetablelength > super->index) &&
			 *         (sub->vftbl->interfacetable[-super->index] != NULL);
			 *
			 *  superclass is a class:
			 *
			 *  return ((sub != NULL) && (0
			 *          <= (sub->vftbl->baseval - super->vftbl->baseval) <=
			 *          super->vftbl->diffvall));
			 */

			{
			classinfo *super;
			vftbl_t   *supervftbl;
			s4         superindex;

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				super = NULL;
				superindex = 0;
				supervftbl = NULL;
			}
			else {
				super = iptr->sx.s23.s3.c.cls;
				superindex = super->index;
				supervftbl = super->vftbl;
			}
			
#if defined(ENABLE_THREADS)
            codegen_threadcritrestart(cd, cd->mcodeptr - cd->mcodebase);
#endif
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s1 == d) {
				M_MOV(s1, REG_ITMP1);
				s1 = REG_ITMP1;
			}

			/* calculate interface instanceof code size */

			s2 = 8;
			if (!super)
				s2 += (opt_showdisassemble ? 1 : 0);

			/* calculate class instanceof code size */

			s3 = 10;
			if (!super)
				s3 += (opt_showdisassemble ? 1 : 0);

			M_CLR(d);

			/* if class is not resolved, check which code to call */

			if (!super) {
				M_TST(s1);
				M_BEQ(3 + (opt_showdisassemble ? 1 : 0) + s2 + 1 + s3);

				disp = dseg_adds4(cd, 0);                     /* super->flags */

				codegen_addpatchref(cd, PATCHER_checkcast_instanceof_flags,
									iptr->sx.s23.s3.c.ref, disp);

				if (opt_showdisassemble)
					M_NOP;

				M_ILD(REG_ITMP3, REG_PV, disp);
				M_AND_IMM(REG_ITMP3, ACC_INTERFACE, REG_ITMP3);
				M_BEQ(s2 + 1);
			}

			/* interface instanceof code */

			if (!super || (super->flags & ACC_INTERFACE)) {
				if (super) {
					M_TST(s1);
					M_BEQ(s2);

				} else {
					codegen_addpatchref(cd,
										PATCHER_checkcast_instanceof_interface,
										iptr->sx.s23.s3.c.ref, 0);

					if (opt_showdisassemble)
						M_NOP;
				}

				M_ALD(REG_ITMP1, s1, OFFSET(java_objectheader, vftbl));
				M_ILD(REG_ITMP3, REG_ITMP1, OFFSET(vftbl_t, interfacetablelength));
				M_LDATST(REG_ITMP3, REG_ITMP3, -superindex);
				M_BLE(4);
				M_ALD(REG_ITMP1, REG_ITMP1,
					  OFFSET(vftbl_t, interfacetable[0]) -
					  superindex * sizeof(methodptr*));
				M_TST(REG_ITMP1);
				M_BEQ(1);
				M_IADD_IMM(REG_ZERO, 1, d);

				if (!super)
					M_BR(s3);
			}

			/* class instanceof code */

			if (!super || !(super->flags & ACC_INTERFACE)) {
				disp = dseg_addaddress(cd, supervftbl);

				if (super) {
					M_TST(s1);
					M_BEQ(s3);

				} else {
					codegen_addpatchref(cd, PATCHER_instanceof_class,
										iptr->sx.s23.s3.c.ref,
										disp);

					if (opt_showdisassemble) {
						M_NOP;
					}
				}

				M_ALD(REG_ITMP1, s1, OFFSET(java_objectheader, vftbl));
				M_ALD(REG_ITMP2, REG_PV, disp);
#if defined(ENABLE_THREADS)
				codegen_threadcritstart(cd, cd->mcodeptr - cd->mcodebase);
#endif
				M_ILD(REG_ITMP1, REG_ITMP1, OFFSET(vftbl_t, baseval));
				M_ILD(REG_ITMP3, REG_ITMP2, OFFSET(vftbl_t, baseval));
				M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, diffval));
#if defined(ENABLE_THREADS)
				codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
				M_SUB(REG_ITMP1, REG_ITMP3, REG_ITMP1);
				M_CMPU(REG_ITMP1, REG_ITMP2);
				M_CLR(d);
				M_BGT(1);
				M_IADD_IMM(REG_ZERO, 1, d);
			}
			emit_store_dst(jd, iptr, d);
			}
			break;

		case ICMD_MULTIANEWARRAY:/* ..., cnt1, [cnt2, ...] ==> ..., arrayref  */

			/* check for negative sizes and copy sizes to stack if necessary  */

			MCODECHECK((iptr->s1.argcount << 1) + 64);

			for (s1 = iptr->s1.argcount; --s1 >= 0; ) {

				var = VAR(iptr->sx.s23.s2.args[s1]);

				/* copy SAVEDVAR sizes to stack */

				if (!(var->flags & PREALLOC)) {
					s2 = emit_load(jd, iptr, var, REG_ITMP1);
#if defined(__DARWIN__)
					M_IST(s2, REG_SP, LA_SIZE + (s1 + INT_ARG_CNT) * 4);
#else
					M_IST(s2, REG_SP, LA_SIZE + (s1 + 3) * 4);
#endif
				}
			}

			/* a0 = dimension count */

			ICONST(rd->argintregs[0], iptr->s1.argcount);

			/* is patcher function set? */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				disp = dseg_addaddress(cd, NULL);

				codegen_addpatchref(cd, PATCHER_builtin_multianewarray,
									iptr->sx.s23.s3.c.ref, disp);

				if (opt_showdisassemble)
					M_NOP;

			} else {
				disp = dseg_addaddress(cd, iptr->sx.s23.s3.c.cls);
			}

			/* a1 = arraydescriptor */

			M_ALD(rd->argintregs[1], REG_PV, disp);

			/* a2 = pointer to dimensions = stack pointer */

#if defined(__DARWIN__)
			M_LDA(rd->argintregs[2], REG_SP, LA_SIZE + INT_ARG_CNT * 4);
#else
			M_LDA(rd->argintregs[2], REG_SP, LA_SIZE + 3 * 4);
#endif

			disp = dseg_addaddress(cd, BUILTIN_multianewarray);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_MTCTR(REG_ITMP3);
			M_JSR;

			/* check for exception before result assignment */

			M_CMPI(REG_RESULT, 0);
			M_BEQ(0);
			codegen_add_fillinstacktrace_ref(cd);

			d = codegen_reg_of_dst(jd, iptr, REG_RESULT);
			M_INTMOVE(REG_RESULT, d);
			emit_store_dst(jd, iptr, d);
			break;

		default:
			*exceptionptr =
				new_internalerror("Unknown ICMD %d during code generation",
								  iptr->opc);
			return false;
	} /* switch */
		
	} /* for instruction */
		
	} /* if (bptr -> flags >= BBREACHED) */
	} /* for basic block */

	dseg_createlinenumbertable(cd);


	/* generate exception and patcher stubs */

	{
		exceptionref *eref;
		patchref     *pref;
		u4            mcode;
		u1           *savedmcodeptr;
		u1           *tmpmcodeptr;

		savedmcodeptr = NULL;

		/* generate exception stubs */

		for (eref = cd->exceptionrefs; eref != NULL; eref = eref->next) {
			gen_resolvebranch(cd->mcodebase + eref->branchpos, 
							  eref->branchpos, cd->mcodeptr - cd->mcodebase);

			MCODECHECK(100);

			/* Check if the exception is an
			   ArrayIndexOutOfBoundsException.  If so, move index register
			   into REG_ITMP1. */

			if (eref->reg != -1)
				M_MOV(eref->reg, REG_ITMP1);

			/* calcuate exception address */

			M_LDA(REG_ITMP2_XPC, REG_PV, eref->branchpos - 4);

			/* move function to call into REG_ITMP3 */

			disp = dseg_addaddress(cd, eref->function);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_ALD(REG_ITMP3, REG_ITMP3, 0);	/* TOC */

			if (savedmcodeptr != NULL) {
				disp = ((u4 *) savedmcodeptr) - (((u4 *) cd->mcodeptr) + 1);
				M_BR(disp);

			} else {
				savedmcodeptr = cd->mcodeptr;

				if (jd->isleafmethod) {
					M_MFLR(REG_ZERO);
					M_AST(REG_ZERO, REG_SP, stackframesize * 8 + LA_LR_OFFSET);
				}

				M_MOV(REG_PV, rd->argintregs[0]);
				M_MOV(REG_SP, rd->argintregs[1]);

				if (jd->isleafmethod)
					M_MOV(REG_ZERO, rd->argintregs[2]);
				else
					M_ALD(rd->argintregs[2],
						  REG_SP, stackframesize * 8 + LA_LR_OFFSET);

				M_MOV(REG_ITMP2_XPC, rd->argintregs[3]);
				M_MOV(REG_ITMP1, rd->argintregs[4]);

				M_STDU(REG_SP, REG_SP, -(LA_SIZE + 6 * 8));
				M_AST(REG_ITMP2_XPC, REG_SP, LA_SIZE + 5 * 8);

				M_MTCTR(REG_ITMP3);
				M_JSR;
				M_MOV(REG_RESULT, REG_ITMP1_XPTR);

				M_ALD(REG_ITMP2_XPC, REG_SP, LA_SIZE + 5 * 8);
				M_LADD_IMM(REG_SP, LA_SIZE + 6 * 8, REG_SP);

				if (jd->isleafmethod) {
					/* XXX FIXME: REG_ZERO can cause problems here! */
					assert(stackframesize * 8 <= 32767);

					M_ALD(REG_ZERO, REG_SP, stackframesize * 8 + LA_LR_OFFSET);
					M_MTLR(REG_ZERO);
				}

				disp = dseg_addaddress(cd, asm_handle_exception);
				M_ALD(REG_ITMP3, REG_PV, disp);
				M_MTCTR(REG_ITMP3);
				M_RTS;
			}
		}


		/* generate code patching stub call code */

		for (pref = cd->patchrefs; pref != NULL; pref = pref->next) {
			/* check code segment size */

			MCODECHECK(16);

			/* Get machine code which is patched back in later. The
			   call is 1 instruction word long. */

			tmpmcodeptr = (u1 *) (cd->mcodebase + pref->branchpos);

			mcode = *((u4 *) tmpmcodeptr);

			/* Patch in the call to call the following code (done at
			   compile time). */

			savedmcodeptr = cd->mcodeptr;   /* save current mcodeptr          */
			cd->mcodeptr  = tmpmcodeptr;    /* set mcodeptr to patch position */

			disp = ((u4 *) savedmcodeptr) - (((u4 *) tmpmcodeptr) + 1);
			M_BR(disp);

			cd->mcodeptr = savedmcodeptr;   /* restore the current mcodeptr   */

			/* create stack frame - keep stack 16-byte aligned */

			M_AADD_IMM(REG_SP, -8 * 8, REG_SP);

			/* calculate return address and move it onto the stack */

			M_LDA(REG_ITMP3, REG_PV, pref->branchpos);
			M_AST_INTERN(REG_ITMP3, REG_SP, 5 * 8);

			/* move pointer to java_objectheader onto stack */

#if defined(ENABLE_THREADS)
			/* order reversed because of data segment layout */

			(void) dseg_addaddress(cd, NULL);                         /* flcword    */
			(void) dseg_addaddress(cd, lock_get_initial_lock_word()); /* monitorPtr */
			disp = dseg_addaddress(cd, NULL);                         /* vftbl      */

			M_LDA(REG_ITMP3, REG_PV, disp);
			M_AST_INTERN(REG_ITMP3, REG_SP, 4 * 8);
#else
			/* do nothing */
#endif

			/* move machine code onto stack */

			disp = dseg_adds4(cd, mcode);
			M_ILD(REG_ITMP3, REG_PV, disp);
			M_IST_INTERN(REG_ITMP3, REG_SP, 3 * 8);

			/* move class/method/field reference onto stack */

			disp = dseg_addaddress(cd, pref->ref);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_AST_INTERN(REG_ITMP3, REG_SP, 2 * 8);

			/* move data segment displacement onto stack */

			disp = dseg_addaddress(cd, pref->disp);
			M_LLD(REG_ITMP3, REG_PV, disp);
			M_IST_INTERN(REG_ITMP3, REG_SP, 1 * 8);

			/* move patcher function pointer onto stack */

			disp = dseg_addaddress(cd, pref->patcher);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_AST_INTERN(REG_ITMP3, REG_SP, 0 * 8);

			disp = dseg_addaddress(cd, asm_patcher_wrapper);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_MTCTR(REG_ITMP3);
			M_RTS;
		}

		/* generate replacement-out stubs */

#if 0
		{
			int i;

			replacementpoint = jd->code->rplpoints;

			for (i = 0; i < jd->code->rplpointcount; ++i, ++replacementpoint) {
				/* check code segment size */

				MCODECHECK(100);

				/* note start of stub code */

				replacementpoint->outcode = (u1 *) (cd->mcodeptr - cd->mcodebase);

				/* make machine code for patching */

				tmpmcodeptr  = cd->mcodeptr;
				cd->mcodeptr = (u1 *) &(replacementpoint->mcode) + 1 /* big-endian */;

				disp = (ptrint)((s4*)replacementpoint->outcode - (s4*)replacementpoint->pc) - 1;
				M_BR(disp);

				cd->mcodeptr = tmpmcodeptr;

				/* create stack frame - keep 16-byte aligned */

				M_AADD_IMM(REG_SP, -4 * 4, REG_SP);

				/* push address of `rplpoint` struct */

				disp = dseg_addaddress(cd, replacementpoint);
				M_ALD(REG_ITMP3, REG_PV, disp);
				M_AST_INTERN(REG_ITMP3, REG_SP, 0 * 4);

				/* jump to replacement function */

				disp = dseg_addaddress(cd, asm_replacement_out);
				M_ALD(REG_ITMP3, REG_PV, disp);
				M_MTCTR(REG_ITMP3);
				M_RTS;
			}
		}
#endif
	}

	codegen_finish(jd);

	/* everything's ok */

	return true;
}


/* createcompilerstub **********************************************************

   Creates a stub routine which calls the compiler.
	
*******************************************************************************/

#define COMPILERSTUB_DATASIZE    3 * SIZEOF_VOID_P
#define COMPILERSTUB_CODESIZE    4 * 4

#define COMPILERSTUB_SIZE        COMPILERSTUB_DATASIZE + COMPILERSTUB_CODESIZE


u1 *createcompilerstub(methodinfo *m)
{
	u1          *s;                     /* memory to hold the stub            */
	ptrint      *d;
	codeinfo    *code;
	codegendata *cd;
	s4           dumpsize;

	s = CNEW(u1, COMPILERSTUB_SIZE);

	/* set data pointer and code pointer */

	d = (ptrint *) s;
	s = s + COMPILERSTUB_DATASIZE;

	/* mark start of dump memory area */

	dumpsize = dump_size();

	cd = DNEW(codegendata);
	cd->mcodeptr = s;

	/* Store the codeinfo pointer in the same place as in the
	   methodheader for compiled methods. */

	code = code_codeinfo_new(m);

	d[0] = (ptrint) asm_call_jit_compiler;
	d[1] = (ptrint) m;
	d[2] = (ptrint) code;

	M_ALD_INTERN(REG_ITMP1, REG_PV, -2 * SIZEOF_VOID_P);
	M_ALD_INTERN(REG_PV, REG_PV, -3 * SIZEOF_VOID_P);
	M_MTCTR(REG_PV);
	M_RTS;

	md_cacheflush((u1 *) d, COMPILERSTUB_SIZE);

#if defined(ENABLE_STATISTICS)
	if (opt_stat)
		count_cstub_len += COMPILERSTUB_SIZE;
#endif

	/* release dump area */

	dump_release(dumpsize);

	return s;
}


/* createnativestub ************************************************************

   Creates a stub routine which calls a native method.

*******************************************************************************/

u1 *createnativestub(functionptr f, jitdata *jd, methoddesc *nmd)
{
	methodinfo   *m;
	codeinfo     *code;
	codegendata  *cd;
	registerdata *rd;
	s4            stackframesize;       /* size of stackframe if needed       */
	methoddesc   *md;
	s4            nativeparams;
	s4            i, j;                 /* count variables                    */
	s4            t;
	s4            s1, s2, disp;
	s4            funcdisp;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	/* set some variables */

	md = m->parseddesc;
	nativeparams = (m->flags & ACC_STATIC) ? 2 : 1;

	/* calculate stackframe size */

	stackframesize =
		sizeof(stackframeinfo) / SIZEOF_VOID_P +
		sizeof(localref_table) / SIZEOF_VOID_P +
		4 +                            /* 4 stackframeinfo arguments (darwin)*/
		nmd->paramcount  + 
		nmd->memuse;

	stackframesize = (stackframesize + 3) & ~3; /* keep stack 16-byte aligned */

	/* create method header */

	(void) dseg_addaddress(cd, code);                      /* CodeinfoPointer */
	(void) dseg_adds4(cd, stackframesize * 8);             /* FrameSize       */
	(void) dseg_adds4(cd, 0);                              /* IsSync          */
	(void) dseg_adds4(cd, 0);                              /* IsLeaf          */
	(void) dseg_adds4(cd, 0);                              /* IntSave         */
	(void) dseg_adds4(cd, 0);                              /* FltSave         */
	(void) dseg_addlinenumbertablesize(cd);
	(void) dseg_adds4(cd, 0);                              /* ExTableSize     */

	/* generate code */

	M_MFLR(REG_ZERO);
	M_AST_INTERN(REG_ZERO, REG_SP, LA_LR_OFFSET);
	M_STDU(REG_SP, REG_SP, -(stackframesize * 8));

#if !defined(NDEBUG)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd)) {
		emit_verbosecall_enter(jd);
	}
#endif
	/* get function address (this must happen before the stackframeinfo) */

	funcdisp = dseg_addaddress(cd, f);

#if !defined(WITH_STATIC_CLASSPATH)
	if (f == NULL) {
		codegen_addpatchref(cd, PATCHER_resolve_native, m, funcdisp);

		if (opt_showdisassemble)
			M_NOP;
	}
#endif

	/* save integer and float argument registers */

	j = 0;

	for (i = 0; i < md->paramcount; i++) {
		t = md->paramtypes[i].type;

		if (IS_INT_LNG_TYPE(t)) {
			if (!md->params[i].inmemory) {
				s1 = md->params[i].regoff;
				M_LST(rd->argintregs[s1], REG_SP, LA_SIZE + PA_SIZE + 4 * 8 + j * 8);
				j++;
			}
		}
	}

	for (i = 0; i < md->paramcount; i++) {
		if (IS_FLT_DBL_TYPE(md->paramtypes[i].type)) {
			if (!md->params[i].inmemory) {
				s1 = md->params[i].regoff;
				M_DST(rd->argfltregs[s1], REG_SP, LA_SIZE + PA_SIZE + 4 * 8 + j * 8);
				j++;
			}
		}
	}

	/* create native stack info */

	M_AADD_IMM(REG_SP, stackframesize * 8, rd->argintregs[0]);
	M_MOV(REG_PV, rd->argintregs[1]);
	M_AADD_IMM(REG_SP, stackframesize * 8, rd->argintregs[2]);
	M_ALD(rd->argintregs[3], REG_SP, stackframesize * 8 + LA_LR_OFFSET);
	disp = dseg_addaddress(cd, codegen_start_native_call);

	M_ALD(REG_ITMP1, REG_PV, disp);
	M_ALD(REG_ITMP1, REG_ITMP1, 0);		/* TOC */
	M_MTCTR(REG_ITMP1);
	M_JSR;

	/* restore integer and float argument registers */

	j = 0;

	for (i = 0; i < md->paramcount; i++) {
		t = md->paramtypes[i].type;

		if (IS_INT_LNG_TYPE(t)) {
			if (!md->params[i].inmemory) {
				s1 = md->params[i].regoff;
				M_LLD(rd->argintregs[s1], REG_SP, LA_SIZE + PA_SIZE + 4 * 8 + j * 8);
				j++;
			}
		}
	}

	for (i = 0; i < md->paramcount; i++) {
		if (IS_FLT_DBL_TYPE(md->paramtypes[i].type)) {
			if (!md->params[i].inmemory) {
				s1 = md->params[i].regoff;
				M_DLD(rd->argfltregs[s1], REG_SP, LA_SIZE + PA_SIZE + 4 * 8 + j * 8);
				j++;
			}
		}
	}
	
	/* copy or spill arguments to new locations */

	for (i = md->paramcount - 1, j = i + nativeparams; i >= 0; i--, j--) {
		t = md->paramtypes[i].type;

		if (IS_INT_LNG_TYPE(t)) {
			if (!md->params[i].inmemory) {
				s1 = rd->argintregs[md->params[i].regoff];

				if (!nmd->params[j].inmemory) {
					s2 = rd->argintregs[nmd->params[j].regoff];
					M_INTMOVE(s1, s2);
				} else {
					s2 = nmd->params[j].regoff;
					M_LST(s1, REG_SP, s2 * 8);
				}

			} else {
				s1 = md->params[i].regoff + stackframesize;
				s2 = nmd->params[j].regoff;

				M_LLD(REG_ITMP1, REG_SP, s1 * 8);
				M_LST(REG_ITMP1, REG_SP, s2 * 8);
			}

		} else {
			/* We only copy spilled float arguments, as the float
			   argument registers keep unchanged. */

			if (md->params[i].inmemory) {
				s1 = md->params[i].regoff + stackframesize;
				s2 = nmd->params[j].regoff;

				if (IS_2_WORD_TYPE(t)) {
					M_DLD(REG_FTMP1, REG_SP, s1 * 8);
					M_DST(REG_FTMP1, REG_SP, s2 * 8);

				} else {
					M_FLD(REG_FTMP1, REG_SP, s1 * 8);
					M_FST(REG_FTMP1, REG_SP, s2 * 8);
				}
			}
		}
	}

	/* put class into second argument register */

	if (m->flags & ACC_STATIC) {
		disp = dseg_addaddress(cd, m->class);
		M_ALD(rd->argintregs[1], REG_PV, disp);
	}

	/* put env into first argument register */

	disp = dseg_addaddress(cd, _Jv_env);
	M_ALD(rd->argintregs[0], REG_PV, disp);

	/* generate the actual native call */
	/* native functions have a different TOC for sure */

	M_AST(REG_TOC, REG_SP, 40);	/* save old TOC */
	M_ALD(REG_ITMP3, REG_PV, funcdisp);
	M_ALD(REG_TOC, REG_ITMP3, 8);	/* load TOC from func. descriptor */
	M_ALD(REG_ITMP3, REG_ITMP3, 0);		
	M_MTCTR(REG_ITMP3);
	M_JSR;
	M_ALD(REG_TOC, REG_SP, 40);	/* restore TOC */

	M_NOP;
	M_NOP;
	M_NOP;

	/* save return value */

	if (md->returntype.type != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(md->returntype.type)) {
			M_LST(REG_RESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);
		}
		else {
			if (IS_2_WORD_TYPE(md->returntype.type))
				M_DST(REG_FRESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);
			else
				M_FST(REG_FRESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);	/* FIXME, needed ?*/
		}
	}

	/* print call trace */
#if ! defined(NDEBGUU)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd)) {
		emit_verbosecall_exit(jd);
	}
#endif
	/* remove native stackframe info */

	M_NOP;
	M_NOP;
	M_NOP;

	M_AADD_IMM(REG_SP, stackframesize * 8, rd->argintregs[0]);
	disp = dseg_addaddress(cd, codegen_finish_native_call);
	M_ALD(REG_ITMP1, REG_PV, disp);
	M_ALD(REG_ITMP1, REG_ITMP1, 0);	/* XXX what about TOC? */
	M_MTCTR(REG_ITMP1);
	M_JSR;
	M_MOV(REG_RESULT, REG_ITMP1_XPTR);

	/* restore return value */

	if (md->returntype.type != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(md->returntype.type)) {
			M_LLD(REG_RESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);
		}
		else {
			if (IS_2_WORD_TYPE(md->returntype.type))
				M_DLD(REG_FRESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);
			else
				M_FLD(REG_FRESULT, REG_SP, LA_SIZE + PA_SIZE + 1 * 8);
		}
	}

	M_ALD(REG_ITMP2_XPC, REG_SP, stackframesize * 8 + LA_LR_OFFSET);
	M_MTLR(REG_ITMP2_XPC);
	M_LDA(REG_SP, REG_SP, stackframesize * 8); /* remove stackframe           */

	/* check for exception */

	M_TST(REG_ITMP1_XPTR);
	M_BNE(1);                           /* if no exception then return        */

	M_RET;

	/* handle exception */

	M_LADD_IMM(REG_ITMP2_XPC, -4, REG_ITMP2_XPC);  /* exception address       */

	disp = dseg_addaddress(cd, asm_handle_nat_exception);
	M_ALD(REG_ITMP3, REG_PV, disp);
	M_MTCTR(REG_ITMP3);
	M_RTS;

	/* generate patcher stub call code */

	{
		patchref *pref;
		u4        mcode;
		u1       *savedmcodeptr;
		u1       *tmpmcodeptr;

		for (pref = cd->patchrefs; pref != NULL; pref = pref->next) {
			/* Get machine code which is patched back in later. The
			   call is 1 instruction word long. */

			tmpmcodeptr = cd->mcodebase + pref->branchpos;

			mcode = *((u4 *) tmpmcodeptr);

			/* Patch in the call to call the following code (done at
			   compile time). */

			savedmcodeptr = cd->mcodeptr;   /* save current mcodeptr          */
			cd->mcodeptr  = tmpmcodeptr;    /* set mcodeptr to patch position */

			disp = ((u4 *) savedmcodeptr) - (((u4 *) tmpmcodeptr) + 1);
			M_BL(disp);

			cd->mcodeptr = savedmcodeptr;   /* restore the current mcodeptr   */

			/* create stack frame - keep stack 16-byte aligned */

			M_AADD_IMM(REG_SP, -8 * 8, REG_SP);

			/* move return address onto stack */

			M_MFLR(REG_ZERO);
			M_AST(REG_ZERO, REG_SP, 5 * 8);

			/* move pointer to java_objectheader onto stack */

#if defined(ENABLE_THREADS)
			/* order reversed because of data segment layout */

			(void) dseg_addaddress(cd, NULL);                         /* flcword    */
			(void) dseg_addaddress(cd, lock_get_initial_lock_word()); /* monitorPtr */
			disp = dseg_addaddress(cd, NULL);                         /* vftbl      */

			M_LDA(REG_ITMP3, REG_PV, disp);
			M_AST(REG_ITMP3, REG_SP, 4 * 8);
#else
			/* do nothing */
#endif

			/* move machine code onto stack */

			disp = dseg_adds4(cd, mcode);
			M_ILD(REG_ITMP3, REG_PV, disp);
			M_IST(REG_ITMP3, REG_SP, 3 * 8);

			/* move class/method/field reference onto stack */

			disp = dseg_addaddress(cd, pref->ref);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_AST(REG_ITMP3, REG_SP, 2 * 8);

			/* move data segment displacement onto stack */

			disp = dseg_adds4(cd, pref->disp);
			M_ILD(REG_ITMP3, REG_PV, disp);
			M_IST(REG_ITMP3, REG_SP, 1 * 8);

			/* move patcher function pointer onto stack */

			disp = dseg_addaddress(cd, pref->patcher);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_AST(REG_ITMP3, REG_SP, 0 * 8);

			disp = dseg_addaddress(cd, asm_patcher_wrapper);
			M_ALD(REG_ITMP3, REG_PV, disp);
			M_MTCTR(REG_ITMP3);
			M_RTS;
		}
	}

	codegen_finish(jd);

	return code->entrypoint;
}




/*
 * These are local overrides for various environment variables in Emacs.
 * Please do not remove this and leave it at the end of the file, where
 * Emacs will automagically detect them.
 * ---------------------------------------------------------------------
 * Local variables:
 * mode: c
 * indent-tabs-mode: t
 * c-basic-offset: 4
 * tab-width: 4
 * End:
 * vim:noexpandtab:sw=4:ts=4:
 */
