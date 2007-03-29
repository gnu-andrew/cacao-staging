/* src/vm/jit/x86_64/codegen.c - machine code generator for x86_64

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
            Christian Thalinger
            Christian Ullrich
            Edwin Steiner

   $Id: codegen.c 7604 2007-03-29 06:52:15Z pm $

*/


#include "config.h"

#include <assert.h>
#include <stdio.h>

#include "vm/types.h"

#include "md-abi.h"

#include "vm/jit/s390/arch.h"
#include "vm/jit/s390/codegen.h"
#include "vm/jit/s390/emit.h"

#include "mm/memory.h"
#include "native/jni.h"
#include "native/native.h"

#if defined(ENABLE_THREADS)
# include "threads/native/lock.h"
#endif

#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/global.h"
#include "vmcore/loader.h"
#include "vmcore/options.h"
#include "vmcore/statistics.h"
#include "vm/stringlocal.h"
#include "vm/vm.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/codegen-common.h"
#include "vm/jit/dseg.h"
#include "vm/jit/emit-common.h"
#include "vm/jit/jit.h"
#include "vm/jit/methodheader.h"
#include "vm/jit/parse.h"
#include "vm/jit/patcher.h"
#include "vm/jit/reg.h"
#include "vm/jit/replace.h"
#include "vm/jit/stacktrace.h"

#if defined(ENABLE_LSRA)
# include "vm/jit/allocator/lsra.h"
#endif

#define OOPS() assert(0);

void panic() { }

#if 0
u1 *createcompilerstub(methodinfo *m) {
	OOPS();
	u1 *stub = malloc(8);
	bzero(stub, 8);
	return stub;
}
#endif

#if 0
u1 *createnativestub(functionptr f, jitdata *jd, methoddesc *nmd) {
	OOPS();
	return createcompilerstub(NULL);
}
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
	u2                  currentline;
	ptrint              a;
	varinfo            *var, *var1, *var2, *dst;
	basicblock         *bptr;
	instruction        *iptr;
	exception_entry    *ex;
	constant_classref  *cr;
	unresolved_class   *uc;
	methodinfo         *lm;             /* local methodinfo for ICMD_INVOKE*  */
	unresolved_method  *um;
	builtintable_entry *bte;
	methoddesc         *md;
	fieldinfo          *fi;
	unresolved_field   *uf;
	s4                  fieldtype;
#if 0
	rplpoint           *replacementpoint;
#endif
	s4                 varindex;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	/* prevent compiler warnings */

	d   = 0;
	lm  = NULL;
	um  = NULL;
	bte = NULL;

	{
	s4 i, p, t, l;
	s4 savedregs_num;

  	savedregs_num = 0; 

	/* space to save used callee saved registers */

	savedregs_num += (INT_SAV_CNT - rd->savintreguse);
	savedregs_num += (FLT_SAV_CNT - rd->savfltreguse) * 2;

	cd->stackframesize = rd->memuse + savedregs_num + 1  /* space to save RA */;

	/* CAUTION:
	 * As REG_ITMP3 == REG_RA, do not touch REG_ITMP3, until it has been saved.
	 */

#if defined(ENABLE_THREADS)
	/* space to save argument of monitor_enter */
	OOPS(); /* see powerpc  */
#if 0
	if (checksync && (m->flags & ACC_SYNCHRONIZED))
		cd->stackframesize++;
#endif
#endif

	/* Keep stack of non-leaf functions 16-byte aligned for calls into
	   native code e.g. libc or jni (alignment problems with
	   movaps). */

	if (!jd->isleafmethod || opt_verbosecall )
		/* TODO really 16 bytes ? */
		cd->stackframesize = (cd->stackframesize + 3) & ~3;

	/* create method header */

	(void) dseg_add_unique_address(cd, code);              /* CodeinfoPointer */
	(void) dseg_add_unique_s4(cd, cd->stackframesize * 4); /* FrameSize       */

#if defined(ENABLE_THREADS)
	/* IsSync contains the offset relative to the stack pointer for the
	   argument of monitor_exit used in the exception handler. Since the
	   offset could be zero and give a wrong meaning of the flag it is
	   offset by one.
	*/

	if (checksync && (m->flags & ACC_SYNCHRONIZED))
		(void) dseg_add_unique_s4(cd, (rd->memuse + 1) * 4); /* IsSync        */
	else
#endif
/*
		(void) dseg_add_unique_s4(cd, 0);*/                  /* IsSync          */

	disp = dseg_add_unique_address(cd, 0);

	(void) dseg_add_unique_s4(cd, jd->isleafmethod);               /* IsLeaf  */
	(void) dseg_add_unique_s4(cd, INT_SAV_CNT - rd->savintreguse); /* IntSave */
	(void) dseg_add_unique_s4(cd, FLT_SAV_CNT - rd->savfltreguse); /* FltSave */

	(void) dseg_addlinenumbertablesize(cd);

	(void) dseg_add_unique_s4(cd, jd->exceptiontablelength); /* ExTableSize   */

	/* create exception table */

	for (ex = jd->exceptiontable; ex != NULL; ex = ex->down) {
		dseg_add_target(cd, ex->start);
   		dseg_add_target(cd, ex->end);
		dseg_add_target(cd, ex->handler);
		(void) dseg_add_unique_address(cd, ex->catchtype.any);
	}
	
	/* generate method profiling code */

#if defined(ENABLE_PROFILING)
	if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
		/* count frequency */

		M_ALD(REG_ITMP1, REG_PV, CodeinfoPointer);
		M_ILD(REG_ITMP2, REG_ITMP1, OFFSET(codeinfo, frequency));
		M_IADD_IMM(1, REG_ITMP2);
		M_IST(REG_ITMP2, REG_ITMP1, OFFSET(codeinfo, frequency));

/* 		PROFILE_CYCLE_START; */
	}
#endif

	/* create stack frame (if necessary) */

	if (cd->stackframesize)
		M_ASUB_IMM(cd->stackframesize * 4, REG_SP);

	N_LHI(REG_ITMP2, disp);
	N_ST(REG_SP, 0, REG_ITMP2, REG_PV);

	/* save used callee saved registers and return address */

  	p = cd->stackframesize;
	p--; M_AST(REG_RA, REG_SP, p * 4);

	for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
 		p--; M_IST(rd->savintregs[i], REG_SP, p * 4);
	}
	for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
		p -= 2; M_DST(rd->savfltregs[i], REG_SP, p * 4);
	}

	/* take arguments out of register or stack frame */

	md = m->parseddesc;

 	for (p = 0, l = 0; p < md->paramcount; p++) {
 		t = md->paramtypes[p].type;
		varindex = jd->local_map[l * 5 + t];

 		l++;
 		if (IS_2_WORD_TYPE(t))    /* increment local counter for 2 word types */
 			l++;

		if (varindex == UNUSED)
			continue;

		var = VAR(varindex);

		s1 = md->params[p].regoff;
		if (IS_INT_LNG_TYPE(t)) {                    /* integer args          */
			if (IS_2_WORD_TYPE(t))
				s2 = PACK_REGS(rd->argintregs[GET_LOW_REG(s1)],
							   rd->argintregs[GET_HIGH_REG(s1)]);
			else
				s2 = rd->argintregs[s1];
 			if (!md->params[p].inmemory) {           /* register arguments    */
 				if (!IS_INMEMORY(var->flags)) {      /* reg arg -> register   */
					if (IS_2_WORD_TYPE(t))
						M_LNGMOVE(s2, var->vv.regoff);
					else
						M_INTMOVE(s2, var->vv.regoff);

				} else {                             /* reg arg -> spilled    */
					if (IS_2_WORD_TYPE(t))
						M_LST(s2, REG_SP, var->vv.regoff * 4);
					else
						M_IST(s2, REG_SP, var->vv.regoff * 4);
				}

			} else {                                 /* stack arguments       */
 				if (!IS_INMEMORY(var->flags)) {      /* stack arg -> register */
					if (IS_2_WORD_TYPE(t))
						M_LLD(var->vv.regoff, REG_SP, (cd->stackframesize + s1) * 4);
					else
						M_ILD(var->vv.regoff, REG_SP, (cd->stackframesize + s1) * 4);

				} else {                             /* stack arg -> spilled  */
 					M_ILD(REG_ITMP1, REG_SP, (cd->stackframesize + s1) * 4);
 					M_IST(REG_ITMP1, REG_SP, var->vv.regoff * 4);
					if (IS_2_WORD_TYPE(t)) {
						M_ILD(REG_ITMP1, REG_SP, (cd->stackframesize + s1) * 4 +4);
						M_IST(REG_ITMP1, REG_SP, var->vv.regoff * 4 + 4);
					}
				}
			}

		} else {                                     /* floating args         */
 			if (!md->params[p].inmemory) {           /* register arguments    */
				s2 = rd->argfltregs[s1];
 				if (!IS_INMEMORY(var->flags)) {      /* reg arg -> register   */
 					M_FLTMOVE(s2, var->vv.regoff);

 				} else {			                 /* reg arg -> spilled    */
					if (IS_2_WORD_TYPE(t))
						M_DST(s2, REG_SP, var->vv.regoff * 4);
					else
						M_FST(s2, REG_SP, var->vv.regoff * 4);
 				}

 			} else {                                 /* stack arguments       */
 				if (!IS_INMEMORY(var->flags)) {      /* stack-arg -> register */
					if (IS_2_WORD_TYPE(t))
						M_DLD(var->vv.regoff, REG_SP, (cd->stackframesize + s1) * 4);

					else
						M_FLD(var->vv.regoff, REG_SP, (cd->stackframesize + s1) * 4);

 				} else {                             /* stack-arg -> spilled  */
					if (IS_2_WORD_TYPE(t)) {
						M_DLD(REG_FTMP1, REG_SP, (cd->stackframesize + s1) * 4);
						M_DST(REG_FTMP1, REG_SP, var->vv.regoff * 4);
						var->vv.regoff = cd->stackframesize + s1;

					} else {
						M_FLD(REG_FTMP1, REG_SP, (cd->stackframesize + s1) * 4);
						M_FST(REG_FTMP1, REG_SP, var->vv.regoff * 4);
					}
				}
			}
		}
	} /* end for */

	/* save monitorenter argument */

#if defined(ENABLE_THREADS)
	if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
		/* stack offset for monitor argument */

		s1 = rd->memuse;

		if (opt_verbosecall) {
			M_LSUB_IMM((INT_ARG_CNT + FLT_ARG_CNT) * 8, REG_SP);

			for (p = 0; p < INT_ARG_CNT; p++)
				M_LST(rd->argintregs[p], REG_SP, p * 8);

			for (p = 0; p < FLT_ARG_CNT; p++)
				M_DST(rd->argfltregs[p], REG_SP, (INT_ARG_CNT + p) * 8);

			s1 += INT_ARG_CNT + FLT_ARG_CNT;
		}

		/* decide which monitor enter function to call */

		if (m->flags & ACC_STATIC) {
			M_MOV_IMM(&m->class->object.header, REG_A0);
		}
		else {
			M_TEST(REG_A0);
			M_BEQ(0);
			codegen_add_nullpointerexception_ref(cd);
		}

		M_AST(REG_A0, REG_SP, s1 * 8);
		M_MOV_IMM(LOCK_monitor_enter, REG_ITMP1);
		M_CALL(REG_ITMP1);

		if (opt_verbosecall) {
			for (p = 0; p < INT_ARG_CNT; p++)
				M_LLD(rd->argintregs[p], REG_SP, p * 8);

			for (p = 0; p < FLT_ARG_CNT; p++)
				M_DLD(rd->argfltregs[p], REG_SP, (INT_ARG_CNT + p) * 8);

			M_LADD_IMM((INT_ARG_CNT + FLT_ARG_CNT) * 8, REG_SP);
		}
	}
#endif

#if !defined(NDEBUG)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd))
		emit_verbosecall_enter(jd);
#endif /* !defined(NDEBUG) */

	}

	/* end of header generation */
#if 0
	replacementpoint = jd->code->rplpoints;
#endif

	/* walk through all basic blocks */

	for (bptr = jd->basicblocks; bptr != NULL; bptr = bptr->next) {

		bptr->mpc = (u4) ((u1 *) cd->mcodeptr - cd->mcodebase);

		if (bptr->flags >= BBREACHED) {

		/* branch resolving */

		codegen_resolve_branchrefs(cd, bptr);

		/* handle replacement points */

#if 0
		if (bptr->bitflags & BBFLAG_REPLACEMENT) {
			replacementpoint->pc = (u1*)(ptrint)bptr->mpc; /* will be resolved later */
			
			replacementpoint++;

			assert(cd->lastmcodeptr <= cd->mcodeptr);
			cd->lastmcodeptr = cd->mcodeptr + 5; /* 5 byte jmp patch */
		}
#endif

		/* copy interface registers to their destination */

		len = bptr->indepth;
		MCODECHECK(512);

		/* generate basicblock profiling code */

		if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
			/* count frequency */

			M_MOV_IMM(code->bbfrequency, REG_ITMP3);
			M_IINC_MEMBASE(REG_ITMP3, bptr->nr * 4);

			/* if this is an exception handler, start profiling again */

			if (bptr->type == BBTYPE_EXH)
				PROFILE_CYCLE_START;
		}

#if defined(ENABLE_LSRA)
		if (opt_lsra) {
			while (len) {
				len--;
				src = bptr->invars[len];
				if ((len == bptr->indepth-1) && (bptr->type != BBTYPE_STD)) {
					if (bptr->type == BBTYPE_EXH) {
/*  					d = reg_of_var(rd, src, REG_ITMP1); */
						if (!IS_INMEMORY(src->flags))
							d= src->vv.regoff;
						else
							d=REG_ITMP1;
						M_INTMOVE(REG_ITMP1, d);
						emit_store(jd, NULL, src, d);
					}
				}
			}
			
		} else {
#endif

		while (len) {
			len--;
			var = VAR(bptr->invars[len]);
  			if ((len ==  bptr->indepth-1) && (bptr->type != BBTYPE_STD)) {
				if (bptr->type == BBTYPE_EXH) {
					d = codegen_reg_of_var(0, var, REG_ITMP1);
					M_INTMOVE(REG_ITMP1, d);
					emit_store(jd, NULL, var, d);
				}
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

			MCODECHECK(1024);                         /* 1KB should be enough */

		switch (iptr->opc) {
		case ICMD_NOP:        /* ...  ==> ...                                 */
		case ICMD_POP:        /* ..., value  ==> ...                          */
		case ICMD_POP2:       /* ..., value, value  ==> ...                   */
		case ICMD_INLINE_START: /* internal ICMDs                         */
		case ICMD_INLINE_END:
			break;

		case ICMD_CHECKNULL:  /* ..., objectref  ==> ..., objectref           */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_TEST(s1);
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

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			LCONST(d, iptr->sx.val.l);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FCONST:     /* ...  ==> ..., constant                       */
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			disp = dseg_add_float(cd, iptr->sx.val.f);
			M_FLDN(d, REG_PV, disp, REG_ITMP1);
			emit_store_dst(jd, iptr, d);
			break;
		
		case ICMD_DCONST:     /* ...  ==> ..., constant                       */
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			disp = dseg_add_double(cd, iptr->sx.val.d);
			M_DLDN(d, REG_PV, disp, REG_ITMP1);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ACONST:     /* ...  ==> ..., constant                       */
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				cr = iptr->sx.val.c.ref;
				disp = dseg_add_unique_address(cd, cr);

/* 				PROFILE_CYCLE_STOP; */

				codegen_add_patch_ref(cd, PATCHER_resolve_classref_to_classinfo,
									  cr, disp);

/* 				PROFILE_CYCLE_START; */

				M_ALD(d, REG_PV, disp);
			} else {
				if (iptr->sx.val.anyptr == 0) {
					M_CLR(d);
				} else {
					disp = dseg_add_unique_address(cd, iptr->sx.val.anyptr);
					M_ALD(d, REG_PV, disp);
				}
			}
			emit_store_dst(jd, iptr, d);
			break;


		/* load/store/copy/move operations ************************************/

		case ICMD_ILOAD:      /* ...  ==> ..., content of local variable      */
		case ICMD_ALOAD:      /* s1 = local variable                          */
		case ICMD_LLOAD:
		case ICMD_FLOAD:  
		case ICMD_DLOAD:  
		case ICMD_ISTORE:     /* ..., value  ==> ...                          */
		case ICMD_LSTORE:
		case ICMD_FSTORE:
		case ICMD_DSTORE: 
		case ICMD_COPY:
		case ICMD_MOVE:
			emit_copy(jd, iptr, VAROP(iptr->s1), VAROP(iptr->dst));
			break;

		case ICMD_ASTORE:
			if (!(iptr->flags.bits & INS_FLAG_RETADDR))
				emit_copy(jd, iptr, VAROP(iptr->s1), VAROP(iptr->dst));
			break;

		/* integer operations *************************************************/

		case ICMD_INEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1); 
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			N_LCR(d, s1);
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_LNEG:       /* ..., value  ==> ..., - value                 */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1); 
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_LNEG(d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_I2L:        /* ..., value  ==> ..., value                   */

			d = emit_alloc_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2);
			s2 = emit_load_s2(jd, iptr, GET_HIGH_REG(d)); 

			M_INTMOVE(s2, GET_HIGH_REG(d));
			ICONST(GET_LOW_REG(d), 0);
			N_SRDA(GET_HIGH_REG(d), 32, RN);

			emit_copy_dst(jd, iptr, d);
			emit_store_dst(jd, iptr, d);
			emit_restore_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2);

			break;

		case ICMD_L2I:        /* ..., value  ==> ..., value                   */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(GET_LOW_REG(s1), d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INT2BYTE:   /* ..., value  ==> ..., value                   */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_BSEXT(s1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_INT2CHAR:   /* ..., value  ==> ..., value                   */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, d);
			N_SLL(d, 16, RN);
			N_SRL(d, 16, RN);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_INT2SHORT:  /* ..., value  ==> ..., value                   */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_SSEXT(s1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;


		case ICMD_IADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_IADD(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IADD(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_IINC:
		case ICMD_IADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.i = constant                             */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);

			if (N_VALID_IMM(iptr->sx.val.i)) {
				M_IADD_IMM(iptr->sx.val.i, d);
			} else {
				disp = dseg_add_s4(cd, iptr->sx.val.i);
				M_ILD(REG_ITMP2, REG_PV, disp);
				M_IADD(REG_ITMP2, d);	
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_LADD(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_LADD(s2, d);
			}
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_LADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.l = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			if (IS_IMM32(iptr->sx.val.l))
				M_LADD_IMM(iptr->sx.val.l, d);
			else {
				M_MOV_IMM(iptr->sx.val.l, REG_ITMP2);
				M_LADD(REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_ISUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
								 
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d) {
				M_INTMOVE(s1, REG_ITMP1);
				M_ISUB(s2, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
			} else {
				M_INTMOVE(s1, d);
				M_ISUB(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_ISUBCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			/* TODO range */
			if (N_VALID_IMM(iptr->sx.val.i)) {
				M_ISUB_IMM(iptr->sx.val.i, d);
			} else {
				disp = dseg_add_s4(cd, iptr->sx.val.i);
				M_ILD(REG_ITMP2, REG_PV, disp);
				M_ISUB(REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_LSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d) {
				M_INTMOVE(s1, REG_ITMP1);
				M_LSUB(s2, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
			} else {
				M_INTMOVE(s1, d);
				M_LSUB(s2, d);
			}
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_LSUBCONST:  /* ..., value  ==> ..., value - constant        */
		                      /* sx.val.l = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			if (IS_IMM32(iptr->sx.val.l))
				M_LSUB_IMM(iptr->sx.val.l, d);
			else {
				M_MOV_IMM(iptr->sx.val.l, REG_ITMP2);
				M_LSUB(REG_ITMP2, d);
			}
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_IMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_IMUL(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IMUL(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_IMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* sx.val.i = constant                             */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			if (iptr->sx.val.i == 2) {
				M_ISLL_IMM(1, d);
			} else if (N_VALID_IMM(iptr->sx.val.i)) {
				M_IMUL_IMM(iptr->sx.val.i, d);
			} else {
				disp = dseg_add_s4(cd, iptr->sx.val.i);
				M_ILD(REG_ITMP2, REG_PV, disp);
				M_IMUL(REG_ITMP2, d);	
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
		case ICMD_IREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			/* For this operation we need a register pair.
			 * We will use r0 and itmp1 and will backup r0.
			 */

			M_INTMOVE(R0, REG_ITMP3);

			s2 = emit_load_s2_notzero(jd, iptr, REG_ITMP2);

			s1 = emit_load_s1(jd, iptr, R0);
			M_INTMOVE(s1, R0);
			N_LHI(REG_ITMP1, 0);
			N_SRDA(R0, 32, RN);

			N_DR(R0, s2);

			if (iptr->opc == ICMD_IREM) {
				d = codegen_reg_of_dst(jd, iptr, R0);
				M_INTMOVE(R0, d);
			} else {
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
			}

			emit_store_dst(jd, iptr, d);

			M_INTMOVE(REG_ITMP3, R0);

			break;

		case ICMD_IDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP1);
			emit_alul_imm_reg(cd, ALU_CMP, -1, REG_ITMP1);
			emit_leal_membase_reg(cd, REG_ITMP1, (1 << iptr->sx.val.i) - 1, REG_ITMP2);
			emit_cmovccl_reg_reg(cd, CC_LE, REG_ITMP2, REG_ITMP1);
			emit_shiftl_imm_reg(cd, SHIFT_SAR, iptr->sx.val.i, REG_ITMP1);
			emit_mov_reg_reg(cd, REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_IREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP1);
			emit_alul_imm_reg(cd, ALU_CMP, -1, REG_ITMP1);
			emit_leal_membase_reg(cd, REG_ITMP1, iptr->sx.val.i, REG_ITMP2);
			emit_cmovccl_reg_reg(cd, CC_G, REG_ITMP1, REG_ITMP2);
			emit_alul_imm_reg(cd, ALU_AND, -1 - (iptr->sx.val.i), REG_ITMP2);
			emit_alul_reg_reg(cd, ALU_SUB, REG_ITMP2, REG_ITMP1);
			emit_mov_reg_reg(cd, REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;


		case ICMD_LDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP1);
			emit_alu_imm_reg(cd, ALU_CMP, -1, REG_ITMP1);
			emit_lea_membase_reg(cd, REG_ITMP1, (1 << iptr->sx.val.i) - 1, REG_ITMP2);
			emit_cmovcc_reg_reg(cd, CC_LE, REG_ITMP2, REG_ITMP1);
			emit_shift_imm_reg(cd, SHIFT_SAR, iptr->sx.val.i, REG_ITMP1);
			emit_mov_reg_reg(cd, REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_LREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* sx.val.l = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP1);
			emit_alu_imm_reg(cd, ALU_CMP, -1, REG_ITMP1);
			emit_lea_membase_reg(cd, REG_ITMP1, iptr->sx.val.i, REG_ITMP2);
			emit_cmovcc_reg_reg(cd, CC_G, REG_ITMP1, REG_ITMP2);
			emit_alu_imm_reg(cd, ALU_AND, -1 - (iptr->sx.val.i), REG_ITMP2);
			emit_alu_reg_reg(cd, ALU_SUB, REG_ITMP2, REG_ITMP1);
			emit_mov_reg_reg(cd, REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_ISHL:       /* ..., val1, val2  ==> ..., val1 << val2       */
		case ICMD_ISHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */
		case ICMD_IUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			switch (iptr->opc) {
				case ICMD_ISHL:
					N_SLA(d, 0, s2);
					break;
				case ICMD_ISHR:
					N_SRA(d, 0, s2);
					break;
				case ICMD_IUSHR:
					N_SRL(d, 0, s2);
					break;
				default:
					assert(0);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHLCONST:  /* ..., value  ==> ..., value << constant       */
		                      /* sx.val.i = constant                             */
		case ICMD_ISHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                             */
		case ICMD_IUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
		                      /* sx.val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);

			M_INTMOVE(s1, d);

			if (N_VALID_DISP(iptr->sx.val.i)) {
				disp = iptr->sx.val.i;
				s3 = RN;
			} else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				disp = 0;
				s3 = REG_ITMP3;
			}

			switch (iptr->opc) {
				case ICMD_ISHLCONST:
					N_SLA(d, disp, s3);
					break;
				case ICMD_ISHRCONST:
					N_SRA(d, disp, s3);
					break;
				case ICMD_IUSHRCONST:
					N_SRL(d, disp, s3);
					break;
				default:
					assert(0);
			}

			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LSHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

		case ICMD_LSHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

		case ICMD_LUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			d = emit_alloc_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2);
			s2 = emit_load_s2(jd, iptr, REG_ITMP3); /* d wont contain REG_ITMP3 */

			if ((s2 == GET_LOW_REG(d)) || (s2 == GET_HIGH_REG(d))) {
				M_INTMOVE(s2, REG_ITMP3);
				s2 = REG_ITMP3;
			}

			s1 = emit_load_s1(jd, iptr, d);

			M_LNGMOVE(s1, d);

			switch (iptr->opc) {
				case ICMD_LSHL:
					N_SLDA(GET_HIGH_REG(d), 0, s2);
					break;
				case ICMD_LSHR:
					N_SRDA(GET_HIGH_REG(d), 0, s2);
					break;
				case ICMD_LUSHR:
					N_SRDL(GET_HIGH_REG(d), 0, s2);
					break;
				default:
					assert(0);
			}

			emit_copy_dst(jd, iptr, d);
			emit_store_dst(jd, iptr, d);
			emit_restore_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2);

			break;

        case ICMD_LSHLCONST:  /* ..., value  ==> ..., value << constant       */
 			                  /* sx.val.i = constant                             */
		case ICMD_LSHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                             */
  		case ICMD_LUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
  		                      /* sx.val.l = constant                             */
		
			d = emit_alloc_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2); /* won't contain itmp3 */
			s1 = emit_load_s1(jd, iptr, d);
		
			M_LNGMOVE(s1, d);

			if (N_VALID_DISP(iptr->sx.val.i)) {
				disp = iptr->sx.val.i;
				s3 = RN;
			} else {
				ICONST(REG_ITMP3, iptr->sx.val.i);
				disp = 0;
				s3 = REG_ITMP3;
			}

			switch (iptr->opc) {
				case ICMD_LSHLCONST:
					N_SLDA(GET_HIGH_REG(d), disp, s3);
					break;
				case ICMD_LSHRCONST:
					N_SRDA(GET_HIGH_REG(d), disp, s3);
					break;
				case ICMD_LUSHRCONST:
					N_SRDL(GET_HIGH_REG(d), disp, s3);
					break;
				default:
					assert(0);
			}

			emit_copy_dst(jd, iptr, d);
			emit_store_dst(jd, iptr, d);
			emit_restore_dst_even_odd(jd, iptr, R0, REG_ITMP1, REG_ITMP2);

			break;

		case ICMD_IAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_IAND(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IAND(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_IOR:        /* ..., val1, val2  ==> ..., val1 | val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_IOR(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IOR(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_IXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s2 == d)
				M_IXOR(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IXOR(s2, d);
			}
			emit_store_dst(jd, iptr, d);

			break;



		case ICMD_IANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_IAND_IMM(iptr->sx.val.i, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_LOR:        /* ..., val1, val2  ==> ..., val1 | val2        */
		case ICMD_LXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */
		case ICMD_LAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);

			s1 = emit_load_s1_low(jd, iptr, GET_LOW_REG(d));
			s2 = emit_load_s2_low(jd, iptr, REG_ITMP3);

			M_INTMOVE(s1, GET_LOW_REG(d));

			switch (iptr->opc) {
				case ICMD_LAND:
					M_IAND(s2, GET_LOW_REG(d));
					break;
				case ICMD_LXOR:
					M_IXOR(s2, GET_LOW_REG(d));
					break;
				case ICMD_LOR:
					M_IOR(s2, GET_LOW_REG(d));
					break;
				default:
					assert(0);
			}

			s1 = emit_load_s1_high(jd, iptr, GET_HIGH_REG(d));
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP3);

			M_INTMOVE(s1, GET_HIGH_REG(d));

			switch (iptr->opc) {
				case ICMD_LAND:
					M_IAND(s2, GET_HIGH_REG(d));
					break;
				case ICMD_LXOR:
					M_IXOR(s2, GET_HIGH_REG(d));
					break;
				case ICMD_LOR:
					M_IOR(s2, GET_HIGH_REG(d));
					break;
				default:
					assert(0);
			}

			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.l = constant                             */
		case ICMD_LXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* sx.val.l = constant                             */
		case ICMD_LANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* sx.val.l = constant                             */

			/* TODO should use memory operand to access data segment, not load */

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);

			s1 = emit_load_s1_low(jd, iptr, GET_LOW_REG(d));
			s3 = iptr->sx.val.l & 0xffffffff;

			M_INTMOVE(s1, GET_LOW_REG(d));

			ICONST(REG_ITMP3, s3);

			switch (iptr->opc) {
				case ICMD_LANDCONST:
					M_IAND(REG_ITMP3, GET_LOW_REG(d));
					break;
				case ICMD_LXORCONST:
					M_IXOR(REG_ITMP3, GET_LOW_REG(d));
					break;
				case ICMD_LORCONST:
					M_IOR(REG_ITMP3, GET_LOW_REG(d));
					break;
				default:
					assert(0);
			}

			s1 = emit_load_s1_high(jd, iptr, GET_HIGH_REG(d));
			s3 = iptr->sx.val.l >> 32;

			M_INTMOVE(s1, GET_HIGH_REG(d));

			ICONST(REG_ITMP3, s3);

			switch (iptr->opc) {
				case ICMD_LANDCONST:
					M_IAND(REG_ITMP3, GET_HIGH_REG(d));
					break;
				case ICMD_LXORCONST:
					M_IXOR(REG_ITMP3, GET_HIGH_REG(d));
					break;
				case ICMD_LORCONST:
					M_IOR(REG_ITMP3, GET_HIGH_REG(d));
					break;
				default:
					assert(0);
			}

			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_IORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_IOR_IMM(iptr->sx.val.i, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_IXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* sx.val.i = constant                             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_IXOR_IMM(iptr->sx.val.i, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;


		/* floating operations ************************************************/

		case ICMD_FNEG:       /* ..., value  ==> ..., - value                 */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			disp = dseg_add_s4(cd, 0x80000000);
			M_FLTMOVE(s1, d);
			emit_movss_membase_reg(cd, RIP, -((cd->mcodeptr + 9) - cd->mcodebase) + disp, REG_FTMP2);
			emit_xorps_reg_reg(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_DNEG:       /* ..., value  ==> ..., - value                 */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			disp = dseg_add_s8(cd, 0x8000000000000000);
			M_FLTMOVE(s1, d);
			emit_movd_membase_reg(cd, RIP, -((cd->mcodeptr + 9) - cd->mcodebase) + disp, REG_FTMP2);
			emit_xorpd_reg_reg(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_FADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			if (s2 == d)
				M_FADD(s1, d);
			else {
				M_FLTMOVE(s1, d);
				M_FADD(s2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			if (s2 == d)
				M_DADD(s1, d);
			else {
				M_FLTMOVE(s1, d);
				M_DADD(s2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2_but(jd, iptr, REG_FTMP2, d);

			M_FLTMOVE(s1, d);
			M_FSUB(s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2_but(jd, iptr, REG_FTMP2, d);

			M_FLTMOVE(s1, d);
			M_DSUB(s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			if (s2 == d)
				M_FMUL(s1, d);
			else {
				M_FLTMOVE(s1, d);
				M_FMUL(s2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			if (s2 == d)
				M_DMUL(s1, d);
			else {
				M_FLTMOVE(s1, d);
				M_DMUL(s2, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2_but(jd, iptr, REG_FTMP2, d);

			M_FLTMOVE(s1, d);
			M_FDIV(s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2_but(jd, iptr, REG_FTMP2, d);

			M_FLTMOVE(s1, d);
			M_DDIV(s2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_I2F:       /* ..., value  ==> ..., (float) value            */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_CVTIF(s1, d);
  			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_I2D:       /* ..., value  ==> ..., (double) value           */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_CVTID(s1, d);
  			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_L2F:       /* ..., value  ==> ..., (float) value            */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_CVTLF(s1, d);
  			emit_store_dst(jd, iptr, d);
#endif
			break;
			
		case ICMD_L2D:       /* ..., value  ==> ..., (double) value           */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_CVTLD(s1, d);
  			emit_store_dst(jd, iptr, d);
#endif
			break;
			
		case ICMD_F2I:       /* ..., value  ==> ..., (int) value              */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_CVTFI(s1, d);
			emit_store_dst(jd, iptr, d);
			/* TODO: corner cases ? */
			break;

		case ICMD_D2I:       /* ..., value  ==> ..., (int) value              */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			M_CVTDI(s1, d);
			emit_store_dst(jd, iptr, d);
			/* TODO: corner cases ? */
			break;

		case ICMD_F2L:       /* ..., value  ==> ..., (long) value             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_CVTFL(s1, d);
			M_MOV_IMM(0x8000000000000000, REG_ITMP2);
			M_LCMP(REG_ITMP2, d);                             /* corner cases */
			disp = ((s1 == REG_FTMP1) ? 0 : 5) + 10 + 3 +
				((REG_RESULT == d) ? 0 : 3);
			M_BNE(disp);
			M_FLTMOVE(s1, REG_FTMP1);
			M_MOV_IMM(asm_builtin_f2l, REG_ITMP2);
			M_CALL(REG_ITMP2);
			M_INTMOVE(REG_RESULT, d);
  			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_D2L:       /* ..., value  ==> ..., (long) value             */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_CVTDL(s1, d);
			M_MOV_IMM(0x8000000000000000, REG_ITMP2);
			M_LCMP(REG_ITMP2, d);                             /* corner cases */
			disp = ((s1 == REG_FTMP1) ? 0 : 5) + 10 + 3 +
				((REG_RESULT == d) ? 0 : 3);
			M_BNE(disp);
			M_FLTMOVE(s1, REG_FTMP1);
			M_MOV_IMM(asm_builtin_d2l, REG_ITMP2);
			M_CALL(REG_ITMP2);
			M_INTMOVE(REG_RESULT, d);
  			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_F2D:       /* ..., value  ==> ..., (double) value           */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			M_CVTFD(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_D2F:       /* ..., value  ==> ..., (float) value            */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			M_CVTDF(s1, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_FCMPL:      /* ..., val1, val2  ==> ..., val1 fcmpl val2    */
 			                  /* == => 0, < => 1, > => -1 */
		case ICMD_DCMPL:


		case ICMD_FCMPG:      /* ..., val1, val2  ==> ..., val1 fcmpg val2    */
 			                  /* == => 0, < => 1, > => -1 */
		case ICMD_DCMPG:

			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);

			switch (iptr->opc) {
				case ICMD_FCMPG:
				case ICMD_FCMPL:
					N_CEBR(s1, s2);
					break;
				case ICMD_DCMPG:
				case ICMD_DCMPL:
					N_CDBR(s1, s2);
					break;	
			}

			N_BRC( /* load 1 */
				DD_H | (iptr->opc == ICMD_FCMPG || iptr->opc == ICMD_DCMPG ? DD_O : 0),
				SZ_BRC + SZ_BRC + SZ_BRC
			);

			N_BRC( /* load -1 */
				DD_L | (iptr->opc == ICMD_FCMPL || iptr->opc == ICMD_DCMPL ? DD_O : 0),
				SZ_BRC + SZ_BRC + SZ_LHI + SZ_BRC
			);

			N_BRC( /* load 0 */
				DD_E,
				SZ_BRC + SZ_LHI + SZ_BRC + SZ_LHI + SZ_BRC
			);

			N_LHI(d, 1); /* GT */
			M_BR(SZ_BRC + SZ_LHI + SZ_BRC + SZ_LHI);
			N_LHI(d, -1); /* LT */
			M_BR(SZ_BRC + SZ_LHI);
			N_LHI(d, 0); /* EQ */

			emit_store_dst(jd, iptr, d);

			break;


		/* memory operations **************************************************/

		case ICMD_ARRAYLENGTH: /* ..., arrayref  ==> ..., (int) length        */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			gen_nullptr_check(s1);
			M_ILD(d, s1, OFFSET(java_arrayheader, size));
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_BALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_notzero(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);

			emit_array_checks(cd, iptr, s1, s2);

			N_IC(d, OFFSET(java_bytearray, data[0]), s2, s1);
			/* TODO improove this */
			N_SLL(d, 24, RN);
			N_SRL(d, 24, RN);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_CALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);

			emit_array_checks(cd, iptr, s1, s2);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(1, REG_ITMP2);

			N_LH(d, OFFSET(java_chararray, data[0]), REG_ITMP2, s1);
			emit_store_dst(jd, iptr, d);
			break;			

		case ICMD_SALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);

			emit_array_checks(cd, iptr, s1, s2);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(1, REG_ITMP2);

			N_LH(d, OFFSET(java_shortarray, data[0]), REG_ITMP2, s1);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			
			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(2, REG_ITMP2); /* scale index by 4 */
			N_L(d, OFFSET(java_intarray, data[0]), REG_ITMP2, s1);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP3);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);

			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			emit_array_checks(cd, iptr, s1, s2);
			
			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(3, REG_ITMP2); /* scale index by 8 */

			N_L(GET_HIGH_REG(d) /* evntl. itmp1 */, OFFSET(java_intarray, data[0]), REG_ITMP2, s1);
			N_L(GET_LOW_REG(d) /* evntl. itmp2 */, OFFSET(java_intarray, data[0]) + 4, REG_ITMP2, s1);
			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_FALOAD:     /* ..., arrayref, index  ==> ..., value         */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movss_memindex_reg(cd, OFFSET(java_floatarray, data[0]), s1, s2, 2, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_DALOAD:     /* ..., arrayref, index  ==> ..., value         */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP3);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movsd_memindex_reg(cd, OFFSET(java_doublearray, data[0]), s1, s2, 3, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_AALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			
			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(2, REG_ITMP2); /* scale index by 4 */
			N_L(d, OFFSET(java_objectarray, data[0]), REG_ITMP2, s1);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_BASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_notzero(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);

			N_STC(s3, OFFSET(java_bytearray, data[0]), s2, s1);
			break;

		case ICMD_CASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(1, REG_ITMP2);

			N_STH(s3, OFFSET(java_chararray, data[0]), REG_ITMP2, s1);

			break;

		case ICMD_SASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(1, REG_ITMP2);

			N_STH(s3, OFFSET(java_shortarray, data[0]), REG_ITMP2, s1);
			break;

		case ICMD_IASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(2, REG_ITMP2);

			N_ST(s3, OFFSET(java_intarray, data[0]), REG_ITMP2, s1);
			break;

		case ICMD_LASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(3, REG_ITMP2);

			s3 = emit_load_s3_high(jd, iptr, REG_ITMP3);
			N_ST(s3, OFFSET(java_intarray, data[0]), REG_ITMP2, s1);
			s3 = emit_load_s3_low(jd, iptr, REG_ITMP3);
			N_ST(s3, OFFSET(java_intarray, data[0]) + 4, REG_ITMP2, s1);
			break;

		case ICMD_FASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_FTMP1);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(2, REG_ITMP2);

			N_STE(s3, OFFSET(java_floatarray, data[0]), REG_ITMP2, s1);
			break;

		case ICMD_DASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_FTMP1);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(3, REG_ITMP2);

			N_STD(s3, OFFSET(java_doublearray, data[0]), REG_ITMP2, s1);
			break;

		case ICMD_AASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1_notzero(jd, iptr, REG_A0);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_array_checks(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_A1);

			M_INTMOVE(s1, REG_A0);
			M_INTMOVE(s3, REG_A1);

			disp = dseg_add_functionptr(cd, BUILTIN_canstore);
			ICONST(REG_ITMP3, disp);
			N_L(REG_PV, 0, REG_ITMP3, REG_PV);
			M_ISUB_IMM(96, REG_SP);
			M_JSR(REG_RA, REG_PV);
			M_IADD_IMM(96, REG_SP);
			N_BASR(REG_ITMP1, RN);
			disp = (s4) (cd->mcodeptr - cd->mcodebase);
			M_LDA(REG_PV, REG_ITMP1, -disp);

			M_TEST(REG_RESULT);
			M_BEQ(0);
			codegen_add_arraystoreexception_ref(cd);

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);

			M_INTMOVE(s2, REG_ITMP2);
			M_ISLL_IMM(2, REG_ITMP2);
			N_ST(s3, OFFSET(java_objectarray, data[0]), REG_ITMP2, s1);

			/*
			M_SAADDQ(s2, s1, REG_ITMP1); itmp1 := 4 * s2 + s1
			M_AST(s3, REG_ITMP1, OFFSET(java_objectarray, data[0]));
			*/
			break;


		case ICMD_BASTORECONST: /* ..., arrayref, index  ==> ...              */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movb_imm_memindex(cd, iptr->sx.s23.s3.constval, OFFSET(java_bytearray, data[0]), s1, s2, 0);
#endif
			break;

		case ICMD_CASTORECONST:   /* ..., arrayref, index  ==> ...            */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movw_imm_memindex(cd, iptr->sx.s23.s3.constval, OFFSET(java_chararray, data[0]), s1, s2, 1);
#endif
			break;

		case ICMD_SASTORECONST:   /* ..., arrayref, index  ==> ...            */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movw_imm_memindex(cd, iptr->sx.s23.s3.constval, OFFSET(java_shortarray, data[0]), s1, s2, 1);
#endif
			break;

		case ICMD_IASTORECONST: /* ..., arrayref, index  ==> ...              */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movl_imm_memindex(cd, iptr->sx.s23.s3.constval, OFFSET(java_intarray, data[0]), s1, s2, 2);
#endif
			break;

		case ICMD_LASTORECONST: /* ..., arrayref, index  ==> ...              */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}

			if (IS_IMM32(iptr->sx.s23.s3.constval)) {
				emit_mov_imm_memindex(cd, (u4) (iptr->sx.s23.s3.constval & 0x00000000ffffffff), OFFSET(java_longarray, data[0]), s1, s2, 3);
			} else {
				emit_movl_imm_memindex(cd, (u4) (iptr->sx.s23.s3.constval & 0x00000000ffffffff), OFFSET(java_longarray, data[0]), s1, s2, 3);
				emit_movl_imm_memindex(cd, (u4) (iptr->sx.s23.s3.constval >> 32), OFFSET(java_longarray, data[0]) + 4, s1, s2, 3);
			}
#endif
			break;

		case ICMD_AASTORECONST: /* ..., arrayref, index  ==> ...              */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			if (INSTRUCTION_MUST_CHECK(iptr)) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_imm_memindex(cd, 0, OFFSET(java_objectarray, data[0]), s1, s2, 3);
#endif
			break;


		case ICMD_GETSTATIC:  /* ...  ==> ..., value                          */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = dseg_add_unique_address(cd, NULL);

/* 				PROFILE_CYCLE_STOP; */

				codegen_add_patch_ref(cd, PATCHER_get_putstatic, uf, disp);

/* 				PROFILE_CYCLE_START; */
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = dseg_add_address(cd, &(fi->value));

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					PROFILE_CYCLE_STOP;

					codegen_add_patch_ref(cd, PATCHER_clinit, fi->class, 0);

					PROFILE_CYCLE_START;
				}
  			}

			M_ALD(REG_ITMP1, REG_PV, disp);

			switch (fieldtype) {
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ILD(d, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
				M_LLD(d, REG_ITMP1, 0);
				break;
			case TYPE_ADR:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ALD(d, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_FLD(d, REG_ITMP1, 0);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_DLD(d, REG_ITMP1, 0);
				break;
			}

			emit_store_dst(jd, iptr, d);

			break;

		case ICMD_PUTSTATIC:  /* ..., value  ==> ...                          */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = dseg_add_unique_address(cd, uf);

				codegen_addpatchref(cd, PATCHER_get_putstatic, uf, disp);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = dseg_add_address(cd, &(fi->value));

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class))
					codegen_addpatchref(cd, PATCHER_clinit,
										fi->class, disp);
  			}

			M_ALD(REG_ITMP1, REG_PV, disp);
			switch (fieldtype) {
			case TYPE_INT:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_IST(s1, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				s1 = emit_load_s1(jd, iptr, REG_ITMP23_PACKED);
				M_LST(s1, REG_ITMP1, 0);
				break;
			case TYPE_ADR:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_AST(s1, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_FST(s1, REG_ITMP1, 0);
				break;
			case TYPE_DBL:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_DST(s1, REG_ITMP1, 0);
				break;
			}
			break;

		case ICMD_PUTSTATICCONST: /* ...  ==> ...                             */
		                          /* val = value (in current instruction)     */
		                          /* following NOP)                           */
			OOPS();
#if 0
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = dseg_add_unique_address(cd, uf);

				codegen_add_patch_ref(cd, PATCHER_get_putstatic, uf, disp);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = dseg_add_address(cd, &(fi->value));

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class))
					codegen_add_patch_ref(cd, PATCHER_initialize_class, fi->class,
										  0);
  			}
			
			M_ALD(REG_ITMP1, REG_PV, disp);

			switch (fieldtype) {
			case TYPE_INT:
				M_IST(REG_ZERO, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				M_LST(REG_ZERO, REG_ITMP1, 0);
				break;
			case TYPE_ADR:
				M_AST(REG_ZERO, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				M_FST(REG_ZERO, REG_ITMP1, 0);
				break;
			case TYPE_DBL:
				M_DST(REG_ZERO, REG_ITMP1, 0);
				break;
			}
#endif
			break;

		case ICMD_GETFIELD:   /* ...  ==> ..., value                          */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			emit_nullpointer_check(cd, iptr, s1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;

				codegen_addpatchref(cd, PATCHER_get_putfield, uf, 0);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = fi->offset;
			}

			switch (fieldtype) {
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ILD(d, s1, disp);
				break;
			case TYPE_LNG:
   				d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
				if (GET_HIGH_REG(d) == s1) {
					M_ILD(GET_LOW_REG(d), s1, disp + 4);
					M_ILD(GET_HIGH_REG(d), s1, disp);
				}
				else {
					M_ILD(GET_LOW_REG(d), s1, disp + 4);
					M_ILD(GET_HIGH_REG(d), s1, disp);
				}
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

		case ICMD_PUTFIELD:   /* ..., objectref, value  ==> ...               */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			gen_nullptr_check(s1);

			s2 = emit_load_s2(jd, iptr, REG_IFTMP);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;
			} 
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = fi->offset;
			}

			if (IS_INT_LNG_TYPE(fieldtype)) {
				if (IS_2_WORD_TYPE(fieldtype))
					s2 = emit_load_s2(jd, iptr, REG_ITMP23_PACKED);
				else
					s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			}
			else
				s2 = emit_load_s2(jd, iptr, REG_FTMP2);

			if (INSTRUCTION_IS_UNRESOLVED(iptr))
				codegen_addpatchref(cd, PATCHER_get_putfield, uf, 0);

			switch (fieldtype) {
			case TYPE_INT:
				M_IST(s2, s1, disp);
				break;
			case TYPE_LNG:
				/* TODO really order */
				M_IST(GET_LOW_REG(s2), s1, disp + 4);      /* keep this order */
				M_IST(GET_HIGH_REG(s2), s1, disp);         /* keep this order */
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

		case ICMD_PUTFIELDCONST:  /* ..., objectref, value  ==> ...           */
		                          /* val = value (in current instruction)     */
		                          /* following NOP)                           */
			OOPS();
#if 0
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			gen_nullptr_check(s1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;

/* 				PROFILE_CYCLE_STOP; */

				codegen_add_patch_ref(cd, PATCHER_putfieldconst, uf, 0);

/* 				PROFILE_CYCLE_START; */
			} 
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = fi->offset;
			}

			switch (fieldtype) {
			case TYPE_INT:
			case TYPE_FLT:
				M_IST32_IMM(iptr->sx.s23.s2.constval, s1, disp);
				break;
			case TYPE_LNG:
			case TYPE_ADR:
			case TYPE_DBL:
				/* XXX why no check for IS_IMM32? */
				M_IST32_IMM(iptr->sx.s23.s2.constval, s1, disp);
				M_IST32_IMM(iptr->sx.s23.s2.constval >> 32, s1, disp + 4);
				break;
			}
#endif
			break;


		/* branch operations **************************************************/

		case ICMD_ATHROW:       /* ..., objectref ==> ... (, objectref)       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1_XPTR);

			PROFILE_CYCLE_STOP;

#ifdef ENABLE_VERIFIER
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uc = iptr->sx.s23.s2.uc;

				codegen_add_patch_ref(cd, PATCHER_athrow_areturn, uc, 0);
			}
#endif /* ENABLE_VERIFIER */

			disp = dseg_add_functionptr(cd, asm_handle_exception);
			M_ALD(REG_ITMP2, REG_PV, disp);
			M_JMP(REG_ITMP2_XPC, REG_ITMP2);
			M_NOP;

			break;

		case ICMD_GOTO:         /* ... ==> ...                                */
		case ICMD_RET:          /* ... ==> ...                                */

			M_BR(0);
			codegen_add_branch_ref(cd, iptr->dst.block);

			break;

		case ICMD_JSR:          /* ... ==> ...                                */
			OOPS();
#if 0
  			M_JMP_IMM(0);
			codegen_add_branch_ref(cd, iptr->sx.s23.s3.jsrtarget.block);
#endif
			break;
			
		case ICMD_IFNULL:       /* ..., value ==> ...                         */
		case ICMD_IFNONNULL:    /* ..., value ==> ...                         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_TEST(s1);
			switch (iptr->opc) {	
				case ICMD_IFNULL:
					M_BEQ(0);
					break;
				case ICMD_IFNONNULL:
					M_BNE(0);
					break;
			}
			codegen_add_branch_ref(cd, iptr->dst.block);
			break;

		case ICMD_IFEQ:         /* ..., value ==> ...                         */
		case ICMD_IFLT:         /* ..., value ==> ...                         */
		case ICMD_IFLE:         /* ..., value ==> ...                         */
		case ICMD_IFNE:         /* ..., value ==> ...                         */
		case ICMD_IFGT:         /* ..., value ==> ...                         */
		case ICMD_IFGE:         /* ..., value ==> ...                         */
			
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);

			if (N_VALID_IMM(iptr->sx.val.i))
				N_CHI(s1, iptr->sx.val.i);
			else {
				disp = dseg_add_s4(cd, iptr->sx.val.i);
				N_LHI(REG_ITMP2, disp);
				N_C(s1, 0, REG_ITMP2, REG_PV);
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
			codegen_add_branch_ref(cd, iptr->dst.block);

			break;

		case ICMD_IF_LLT:       /* ..., value ==> ...                         */
		case ICMD_IF_LLE:       /* op1 = target JavaVM pc, val.l = constant   */
		case ICMD_IF_LGT:
		case ICMD_IF_LGE:
		case ICMD_IF_LEQ:
		case ICMD_IF_LNE:
			{

				u1 *out_ref = NULL;

				/* ATTENTION: compare high words signed and low words unsigned */
	
				s1 = emit_load_s1_high(jd, iptr, REG_ITMP1);

				if (N_VALID_IMM(iptr->sx.val.l >> 32))
					N_CHI(s1, iptr->sx.val.l >> 32);
				else {
					disp = dseg_add_s4(cd, iptr->sx.val.l >> 32);
					N_LHI(REG_ITMP2, disp);
					N_C(s1, 0, REG_ITMP2, REG_PV);
				}

				switch(iptr->opc) {
				case ICMD_IF_LLT:
				case ICMD_IF_LLE:
					M_BLT(0);
					codegen_addreference(cd, iptr->dst.block);
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BGT(0);
					break;
				case ICMD_IF_LGT:
				case ICMD_IF_LGE:
					M_BGT(0);
					codegen_addreference(cd, iptr->dst.block);
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BLT(0);
					break;
				case ICMD_IF_LEQ: 
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BNE(0);
					break;
				case ICMD_IF_LNE:
					/* EQ ... fall through */
					M_BNE(0);
					codegen_addreference(cd, iptr->dst.block);
					break;
				default:
					assert(0);
				}

				s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);

				disp = dseg_add_s4(cd, (s4)(iptr->sx.val.l & 0xffffffff));
				N_LHI(REG_ITMP2, disp);
				N_CL(s1, 0, REG_ITMP2, REG_PV);

				switch(iptr->opc) {
				case ICMD_IF_LLT:
					M_BLT(0);
					break;
				case ICMD_IF_LLE:
					M_BLE(0);
					break;
				case ICMD_IF_LGT:
					M_BGT(0);
					break;
				case ICMD_IF_LGE:
					M_BGE(0);
					break;
				case ICMD_IF_LEQ:
					M_BEQ(0);
					break;
				case ICMD_IF_LNE:
					M_BNE(0);
					break;
				default:
					assert(0);
				}
				codegen_addreference(cd, iptr->dst.block);

				if (out_ref != NULL) {
					*(u4 *)out_ref |= (u4)(cd->mcodeptr - out_ref) / 2;
				}

			}
			break;

		case ICMD_IF_ACMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPNE:    /* op1 = target JavaVM pc                     */

			/* Compare addresses as 31 bit unsigned integers */

			s1 = emit_load_s1_notzero(jd, iptr, REG_ITMP1);
			N_LA(REG_ITMP1, 0, RN, s1);

			s2 = emit_load_s2_notzero(jd, iptr, REG_ITMP2);
			N_LA(REG_ITMP2, 0, RN, s2);

			M_CMP(REG_ITMP1, REG_ITMP2);

			switch (iptr->opc) {
				case ICMD_IF_ACMPEQ:
					M_BEQ(0);
					break;
				case ICMD_IF_ACMPNE:
					M_BNE(0);
					break;
			}

			codegen_add_branch_ref(cd, iptr->dst.block);
			break;


		case ICMD_IF_ICMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ICMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ICMPLT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ICMPGT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ICMPLE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ICMPGE:    /* ..., value, value ==> ...                  */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			M_CMP(s1, s2);
			switch (iptr->opc) {
				case ICMD_IF_ICMPEQ:
					M_BEQ(0);
					break;
				case ICMD_IF_ICMPNE:
					M_BNE(0);
					break;
				case ICMD_IF_ICMPLT:
					M_BLT(0);
					break;
				case ICMD_IF_ICMPGT:
					M_BGT(0);
					break;
				case ICMD_IF_ICMPLE:
					M_BLE(0);
					break;
				case ICMD_IF_ICMPGE:
					M_BGE(0);
					break;
			}
			codegen_add_branch_ref(cd, iptr->dst.block);

			break;

		case ICMD_IF_LCMPLE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPLT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPGT:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPGE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_LCMPEQ:    /* ..., value, value ==> ...                  */
			{

				u1 *out_ref = NULL;

				/* ATTENTION: compare high words signed and low words unsigned */
	
				s1 = emit_load_s1_high(jd, iptr, REG_ITMP1);
				s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);

				N_CR(s1, s2);

				switch(iptr->opc) {
				case ICMD_IF_LCMPLT:
				case ICMD_IF_LCMPLE:
					M_BLT(0);
					codegen_addreference(cd, iptr->dst.block);
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BGT(0);
					break;
				case ICMD_IF_LCMPGT:
				case ICMD_IF_LCMPGE:
					M_BGT(0);
					codegen_addreference(cd, iptr->dst.block);
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BLT(0);
					break;
				case ICMD_IF_LCMPEQ: 
					/* EQ ... fall through */
					out_ref = cd->mcodeptr;
					M_BNE(0);
					break;
				case ICMD_IF_LCMPNE:
					/* EQ ... fall through */
					M_BNE(0);
					codegen_addreference(cd, iptr->dst.block);
					break;
				default:
					assert(0);
				}

				s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
				s2 = emit_load_s2_low(jd, iptr, REG_ITMP2);
	
				N_CLR(s1, s2);

				switch(iptr->opc) {
				case ICMD_IF_LCMPLT:
					M_BLT(0);
					break;
				case ICMD_IF_LCMPLE:
					M_BLE(0);
					break;
				case ICMD_IF_LCMPGT:
					M_BGT(0);
					break;
				case ICMD_IF_LCMPGE:
					M_BGE(0);
					break;
				case ICMD_IF_LCMPEQ:
					M_BEQ(0);
					break;
				case ICMD_IF_LCMPNE:
					M_BNE(0);
					break;
				default:
					assert(0);
				}

				codegen_addreference(cd, iptr->dst.block);

				if (out_ref != NULL) {
					*(u4 *)out_ref |= (u4)(cd->mcodeptr - out_ref) / 2;
				}

			}
			break;

		case ICMD_IRETURN:      /* ..., retvalue ==> ...                      */

			REPLACEMENT_POINT_RETURN(cd, iptr);
			s1 = emit_load_s1(jd, iptr, REG_RESULT);
			M_INTMOVE(s1, REG_RESULT);
			goto nowperformreturn;

		case ICMD_ARETURN:      /* ..., retvalue ==> ...                      */

			REPLACEMENT_POINT_RETURN(cd, iptr);
			s1 = emit_load_s1(jd, iptr, REG_RESULT);
			M_INTMOVE(s1, REG_RESULT);

#ifdef ENABLE_VERIFIER
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				unresolved_class *uc = iptr->sx.s23.s2.uc;

				codegen_addpatchref(cd, PATCHER_athrow_areturn, uc, 0);
			}
#endif /* ENABLE_VERIFIER */
			goto nowperformreturn;

		case ICMD_LRETURN:      /* ..., retvalue ==> ...                      */

			REPLACEMENT_POINT_RETURN(cd, iptr);
			s1 = emit_load_s1(jd, iptr, REG_RESULT_PACKED);
			M_LNGMOVE(s1, REG_RESULT_PACKED);
			goto nowperformreturn;

		case ICMD_FRETURN:      /* ..., retvalue ==> ...                      */
		case ICMD_DRETURN:

			REPLACEMENT_POINT_RETURN(cd, iptr);
			s1 = emit_load_s1(jd, iptr, REG_FRESULT);
			M_FLTMOVE(s1, REG_FRESULT);
			goto nowperformreturn;

		case ICMD_RETURN:      /* ...  ==> ...                                */

			REPLACEMENT_POINT_RETURN(cd, iptr);

nowperformreturn:
			{
			s4 i, p;
			
			p = cd->stackframesize;

			/* call trace function */

#if !defined(NDEBUG)
			if (JITDATA_HAS_FLAG_VERBOSECALL(jd))
				emit_verbosecall_exit(jd);
#endif /* !defined(NDEBUG) */

#if defined(ENABLE_THREADS)
			if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
				disp = dseg_add_functionptr(cd, LOCK_monitor_exit);
				M_ALD(REG_ITMP3, REG_PV, disp);
				M_CALL(REG_ITMP3);

				/* we need to save the proper return value */

				switch (iptr->opc) {
				case ICMD_LRETURN:
					M_IST(REG_RESULT2, REG_SP, rd->memuse * 4 + 8);
					/* fall through */
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					M_IST(REG_RESULT , REG_SP, rd->memuse * 4 + 4);
					break;
				case ICMD_FRETURN:
					M_FST(REG_FRESULT, REG_SP, rd->memuse * 4 + 4);
					break;
				case ICMD_DRETURN:
					M_DST(REG_FRESULT, REG_SP, rd->memuse * 4 + 4);
					break;
				}

				M_ALD(REG_A0, REG_SP, rd->memuse * 4);
				M_JSR;

				/* and now restore the proper return value */

				switch (iptr->opc) {
				case ICMD_LRETURN:
					M_ILD(REG_RESULT2, REG_SP, rd->memuse * 4 + 8);
					/* fall through */
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					M_ILD(REG_RESULT , REG_SP, rd->memuse * 4 + 4);
					break;
				case ICMD_FRETURN:
					M_FLD(REG_FRESULT, REG_SP, rd->memuse * 4 + 4);
					break;
				case ICMD_DRETURN:
					M_DLD(REG_FRESULT, REG_SP, rd->memuse * 4 + 4);
					break;
				}
			}
#endif

			/* restore return address                                         */

			p--; M_ALD(REG_RA, REG_SP, p * 4);

			/* restore saved registers                                        */

			for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
				p--; M_ILD(rd->savintregs[i], REG_SP, p * 4);
			}
			for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
				p -= 2; M_DLD(rd->savfltregs[i], REG_SP, p * 4);
			}

			/* deallocate stack                                               */

			if (cd->stackframesize)
				M_AADD_IMM(cd->stackframesize * 4, REG_SP);

			M_RET;
			ALIGNCODENOP;
			}
			break;

		case ICMD_TABLESWITCH:  /* ..., index ==> ...                         */
			OOPS();
#if 0
			{
				s4 i, l;
				branch_target_t *table;

				table = iptr->dst.table;

				l = iptr->sx.s23.s2.tablelow;
				i = iptr->sx.s23.s3.tablehigh;

				s1 = emit_load_s1(jd, iptr, REG_ITMP1);
				M_INTMOVE(s1, REG_ITMP1);

				if (l != 0)
					M_ISUB_IMM(l, REG_ITMP1);

				/* number of targets */
				i = i - l + 1;

                /* range check */
				M_ICMP_IMM(i - 1, REG_ITMP1);
				M_BA(0);

				codegen_add_branch_ref(cd, table[0].block); /* default target */

				/* build jump table top down and use address of lowest entry */

				table += i;

				while (--i >= 0) {
					dseg_add_target(cd, table->block);
					--table;
				}

				/* length of dataseg after last dseg_add_target is used
				   by load */

				M_MOV_IMM(0, REG_ITMP2);
				dseg_adddata(cd);
				emit_mov_memindex_reg(cd, -(cd->dseglen), REG_ITMP2, REG_ITMP1, 3, REG_ITMP1);
				M_JMP(REG_ITMP1);
			}
#endif
			break;


		case ICMD_LOOKUPSWITCH: /* ..., key ==> ...                           */
			OOPS();
#if 0
			{
				s4 i;
				lookup_target_t *lookup;

				lookup = iptr->dst.lookup;

				i = iptr->sx.s23.s2.lookupcount;
			
				MCODECHECK(8 + ((7 + 6) * i) + 5);
				s1 = emit_load_s1(jd, iptr, REG_ITMP1);

				while (--i >= 0) {
					M_ICMP_IMM(lookup->value, s1);
					M_BEQ(0);
					codegen_add_branch_ref(cd, lookup->target.block);
					lookup++;
				}

				M_JMP_IMM(0);
			
				codegen_add_branch_ref(cd, iptr->sx.s23.s3.lookupdefault.block);
			}
#endif
			break;


		case ICMD_BUILTIN:      /* ..., [arg1, [arg2 ...]] ==> ...            */

			bte = iptr->sx.s23.s3.bte;
			md  = bte->md;
			goto gen_method;

		case ICMD_INVOKESTATIC: /* ..., [arg1, [arg2 ...]] ==> ...            */
		case ICMD_INVOKESPECIAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
		case ICMD_INVOKEVIRTUAL:/* op1 = arg count, val.a = method pointer    */
		case ICMD_INVOKEINTERFACE:

			REPLACEMENT_POINT_INVOKE(cd, iptr);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				lm = NULL;
				um = iptr->sx.s23.s3.um;
				md = um->methodref->parseddesc.md;
			}
			else {
				lm = iptr->sx.s23.s3.fmiref->p.method;
				um = NULL;
				md = lm->parseddesc;
			}

gen_method:
			s3 = md->paramcount;

			MCODECHECK((s3 << 1) + 64);

			/* copy arguments to registers or stack location */

			for (s3 = s3 - 1; s3 >= 0; s3--) {
				var = VAR(iptr->sx.s23.s2.args[s3]);

				/* Already Preallocated? */
				if (var->flags & PREALLOC)
					continue;

				if (IS_INT_LNG_TYPE(var->type)) {
					if (!md->params[s3].inmemory) {
						if (IS_2_WORD_TYPE(var->type)) {
							s1 = PACK_REGS(
								rd->argintregs[GET_LOW_REG(md->params[s3].regoff)],
								rd->argintregs[GET_HIGH_REG(md->params[s3].regoff)]
							);
							d = emit_load(jd, iptr, var, s1);
							M_LNGMOVE(d, s1);
						}
						else {
							s1 = rd->argintregs[md->params[s3].regoff];
							d = emit_load(jd, iptr, var, s1);
							M_INTMOVE(d, s1);
						}
					}
					else {
						if (IS_2_WORD_TYPE(var->type)) {
							d = emit_load(jd, iptr, var, REG_ITMP12_PACKED);
							M_LST(d, REG_SP, md->params[s3].regoff * 4);
						}
						else {
							d = emit_load(jd, iptr, var, REG_ITMP1);
							M_IST(d, REG_SP, md->params[s3].regoff * 4);
						}
					}
				}
				else {
					if (!md->params[s3].inmemory) {
						s1 = rd->argfltregs[md->params[s3].regoff];
						d = emit_load(jd, iptr, var, s1);
						M_FLTMOVE(d, s1);
					}
					else {
						d = emit_load(jd, iptr, var, REG_FTMP1);
						if (IS_2_WORD_TYPE(var->type))
							M_DST(d, REG_SP, md->params[s3].regoff * 4);
						else
							M_FST(d, REG_SP, md->params[s3].regoff * 4);
					}
				}
			}

			switch (iptr->opc) {
			case ICMD_BUILTIN:
				disp = dseg_add_functionptr(cd, bte->fp);

				N_AHI(REG_SP, -96); /* register save area as required by C abi */	
				N_LHI(REG_ITMP1, disp);
				N_L(REG_PV, 0, REG_ITMP1, REG_PV);
				break;

			case ICMD_INVOKESPECIAL:
				emit_nullpointer_check(cd, iptr, REG_A0);
				M_ILD(REG_ITMP1, REG_A0, 0); /* hardware nullptr   */
				/* fall through */

			case ICMD_INVOKESTATIC:
				if (lm == NULL) {
					disp = dseg_add_unique_address(cd, um);

					codegen_addpatchref(cd, PATCHER_invokestatic_special,
										um, disp);
				}
				else
					disp = dseg_add_address(cd, lm->stubroutine);

				N_LHI(REG_ITMP1, disp);
				N_L(REG_PV, 0, REG_ITMP1, REG_PV);
				break;

			case ICMD_INVOKEVIRTUAL:
				emit_nullpointer_check(cd, iptr, REG_A0);

				if (lm == NULL) {
					codegen_addpatchref(cd, PATCHER_invokevirtual, um, 0);

					s1 = 0;
				}
				else {
					s1 = OFFSET(vftbl_t, table[0]) +
						sizeof(methodptr) * lm->vftblindex;
				}

				M_ALD(REG_METHODPTR, REG_A0, OFFSET(java_objectheader, vftbl));
				M_ALD(REG_PV, REG_METHODPTR, s1);
				break;

			case ICMD_INVOKEINTERFACE:
				emit_nullpointer_check(cd, iptr, REG_A0);

				/* s1 will be negative here, so use (0xFFF + s1) as displacement
				 * and -0xFFF in index register (itmp1)
				 */

				N_LHI(REG_ITMP1, -N_DISP_MAX);

				if (lm == NULL) {
					codegen_addpatchref(cd, PATCHER_invokeinterface, um, 0);

					s1 = 0;
					s2 = 0;
				}
				else {
					s1 = OFFSET(vftbl_t, interfacetable[0]) -
						sizeof(methodptr*) * lm->class->index +
						N_DISP_MAX;

					s2 = sizeof(methodptr) * (lm - lm->class->methods);
				}

				M_ALD(REG_METHODPTR, REG_A0, OFFSET(java_objectheader, vftbl));
				N_L(REG_METHODPTR, s1, REG_ITMP1, REG_METHODPTR);
				M_ALD(REG_PV, REG_METHODPTR, s2);
				break;
			}

			/* generate the actual call */

			M_CALL(REG_PV);
			REPLACEMENT_POINT_INVOKE_RETURN(cd, iptr);
			N_BASR(REG_ITMP1, RN);
			disp = (s4) (cd->mcodeptr - cd->mcodebase);
			M_LDA(REG_PV, REG_ITMP1, -disp);
	
			/* post call finalization */

			if (iptr->opc == ICMD_BUILTIN) {
				N_AHI(REG_SP, 96); /* remove C abi register save area */
			}

			/* actually only used for ICMD_BUILTIN */

			if (INSTRUCTION_MUST_CHECK(iptr)) {
				M_TEST(REG_RESULT);
				M_BEQ(0);
				codegen_add_fillinstacktrace_ref(cd);
			}

			/* store return value */

			d = md->returntype.type;

			if (d != TYPE_VOID) {
				if (IS_INT_LNG_TYPE(d)) {
					if (IS_2_WORD_TYPE(d)) {
						s1 = codegen_reg_of_dst(jd, iptr, REG_RESULT_PACKED);
						M_LNGMOVE(REG_RESULT_PACKED, s1);
					}
					else {
						s1 = codegen_reg_of_dst(jd, iptr, REG_RESULT);
						M_INTMOVE(REG_RESULT, s1);
					}
				}
				else {
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
			 *         super->vftbl->diffval));
			 */

			if (!(iptr->flags.bits & INS_FLAG_ARRAY)) {
				/* object type cast-check */

				classinfo *super;
				vftbl_t   *supervftbl;
				s4         superindex;

				u1        *class_label_refs[] = { 0 }, *class_label;
				u1        *exit_label_refs[] = { 0, 0, 0, 0 };

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

				/* if class is not resolved, check which code to call */

				if (super == NULL) {
					M_TEST(s1);
					exit_label_refs[0] = cd->mcodeptr;
					M_BEQ(0);

					disp = dseg_add_unique_s4(cd, 0);         /* super->flags */

					codegen_add_patch_ref(cd, PATCHER_resolve_classref_to_flags,
										  iptr->sx.s23.s3.c.ref,
										  disp);

					ICONST(REG_ITMP2, ACC_INTERFACE);
					ICONST(REG_ITMP3, disp); /* TODO negative displacement */
					N_N(REG_ITMP2, 0, REG_ITMP3, REG_PV);
					class_label_refs[0] = cd->mcodeptr;
					M_BEQ(0);
				}

				/* interface checkcast code */

				if ((super == NULL) || (super->flags & ACC_INTERFACE)) {
					if (super == NULL) {
						codegen_add_patch_ref(cd,
											  PATCHER_checkcast_instanceof_interface,
											  iptr->sx.s23.s3.c.ref,
											  0);
					} else {
						M_TEST(s1);
						exit_label_refs[1] = cd->mcodeptr;
						M_BEQ(0);
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));
					M_ILD(REG_ITMP3, REG_ITMP2, OFFSET(vftbl_t, interfacetablelength));
					M_ISUB_IMM(superindex, REG_ITMP3);
					M_BLE(0);
					codegen_add_classcastexception_ref(cd, s1);
					N_AHI(
						REG_ITMP2,
						(s4) (OFFSET(vftbl_t, interfacetable[0]) -
							superindex * sizeof(methodptr*))
					);
					M_ALD(REG_ITMP2, REG_ITMP2, 0);
					M_TEST(REG_ITMP2);
					M_BEQ(0);
					codegen_add_classcastexception_ref(cd, s1);

					if (super == NULL) {
						exit_label_refs[2] = cd->mcodeptr;
						M_BR(0);
					}
				}

				/* class checkcast code */

				class_label = cd->mcodeptr;

				if ((super == NULL) || !(super->flags & ACC_INTERFACE)) {
					if (super == NULL) {
						disp = dseg_add_unique_address(cd, NULL);

						codegen_add_patch_ref(cd,
											  PATCHER_resolve_classref_to_vftbl,
											  iptr->sx.s23.s3.c.ref,
											  disp);
					}
					else {
						disp = dseg_add_address(cd, supervftbl);
						M_TEST(s1);
						exit_label_refs[3] = cd->mcodeptr;
						M_BEQ(0);
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));
					M_ALD(REG_ITMP3, REG_PV, disp);
#if defined(ENABLE_THREADS)
					codegen_threadcritstart(cd, cd->mcodeptr - cd->mcodebase);
#endif
					M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, baseval));
					/*  				if (s1 != REG_ITMP1) { */
					/*  					M_ILD(REG_ITMP1, REG_ITMP3, OFFSET(vftbl_t, baseval)); */
					/*  					M_ILD(REG_ITMP3, REG_ITMP3, OFFSET(vftbl_t, diffval)); */
					/*  #if defined(ENABLE_THREADS) */
					/*  					codegen_threadcritstop(cd, (u1 *) mcodeptr - cd->mcodebase); */
					/*  #endif */
					/*  					M_ISUB(REG_ITMP2, REG_ITMP1, REG_ITMP2); */

					/*  				} else { */
					M_ILD(REG_ITMP3, REG_ITMP3, OFFSET(vftbl_t, baseval));
					M_ISUB(REG_ITMP3, REG_ITMP2);
					M_ALD(REG_ITMP3, REG_PV, disp);
					M_ILD(REG_ITMP3, REG_ITMP3, OFFSET(vftbl_t, diffval));
#if defined(ENABLE_THREADS)
					codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
					/*  				} */
					N_CLR(REG_ITMP2, REG_ITMP3); /* Unsigned compare */
					/* M_CMPULE(REG_ITMP2, REG_ITMP3, REG_ITMP3); itmp3 = (itmp2 <= itmp3) */
					M_BGT(0); /* Branch if greater then */
					/* M_BEQZ(REG_ITMP3, 0); branch if (! itmp) -> branch if > */
					codegen_add_classcastexception_ref(cd, s1);
				}

				/* resolve labels by adding the correct displacement */

				for (s2 = 0; s2 < sizeof(exit_label_refs) / sizeof(exit_label_refs[0]); ++s2) {
					if (exit_label_refs[s2])
						*(u4 *)exit_label_refs[s2] |= (u4)(cd->mcodeptr - exit_label_refs[s2]) / 2;
				}

				for (s2 = 0; s2 < sizeof(class_label_refs) / sizeof(class_label_refs[0]); ++s2) {
					if (class_label_refs[s2])
						*(u4 *)class_label_refs[s2] |= (u4)(class_label - class_label_refs[s2]) / 2;
				}

				d = codegen_reg_of_dst(jd, iptr, s1);
			}
			else {
				/* array type cast-check */

				s1 = emit_load_s1(jd, iptr, REG_A0);
				M_INTMOVE(s1, REG_A0);

				if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
					disp = dseg_add_unique_address(cd, NULL);

					codegen_add_patch_ref(cd,
										  PATCHER_resolve_classref_to_classinfo,
										  iptr->sx.s23.s3.c.ref,
										  disp);
				}
				else
					disp = dseg_add_address(cd, iptr->sx.s23.s3.c.cls);

				M_ALD(REG_A1, REG_PV, disp);
				disp = dseg_add_functionptr(cd, BUILTIN_arraycheckcast);
				ICONST(REG_ITMP1, disp); /* TODO negative displacement */
				N_L(REG_PV, 0, REG_ITMP1, REG_PV);
				M_JSR(REG_RA, REG_PV);
				N_BASR(REG_ITMP1, RN);
				disp = (s4) (cd->mcodeptr - cd->mcodebase);
				M_LDA(REG_PV, REG_ITMP1, -disp);

				s1 = emit_load_s1(jd, iptr, REG_ITMP1);
				M_TEST(REG_RESULT);
				M_BEQ(0);
				codegen_add_classcastexception_ref(cd, s1);

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

			u1        *class_label, *class_label_refs[1] = { 0 };
			u1        *exit_label_refs[4] = { 0, 0, 0, 0 };
			u1        *label1, *label1_refs[1] = { 0 };

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				super = NULL;
				superindex = 0;
				supervftbl = NULL;

			} else {
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

			/* if class is not resolved, check which code to call */

			if (super == NULL) {
				M_CLR(d);
				
				M_TEST(s1);
				exit_label_refs[0] = cd->mcodeptr;
				M_BEQ(0);

				disp = dseg_add_unique_s4(cd, 0);             /* super->flags */

				codegen_add_patch_ref(cd, PATCHER_resolve_classref_to_flags,
									  iptr->sx.s23.s3.c.ref, disp);

				ICONST(REG_ITMP2, ACC_INTERFACE);
				ICONST(REG_ITMP3, disp); /* TODO negative displacement */
				N_N(REG_ITMP2, 0, REG_ITMP3, REG_PV);
				class_label_refs[0] = cd->mcodeptr;
				M_BEQ(0);
			}

			/* interface instanceof code */

			if ((super == NULL) || (super->flags & ACC_INTERFACE)) {
				if (super == NULL) {
					/* If d == REG_ITMP2, then it's destroyed in check
					   code above. */
					if (d == REG_ITMP2)
						M_CLR(d);

					codegen_add_patch_ref(cd,
										  PATCHER_checkcast_instanceof_interface,
										  iptr->sx.s23.s3.c.ref, 0);
				}
				else {
					M_CLR(d);
					M_TEST(s1);
					exit_label_refs[1] = cd->mcodeptr;
					M_BEQ(0);
				}

				M_ALD(REG_ITMP1, s1, OFFSET(java_objectheader, vftbl));
				M_ILD(REG_ITMP3, REG_ITMP1, OFFSET(vftbl_t, interfacetablelength));
				M_ISUB_IMM(superindex, REG_ITMP3);
				label1_refs[0] = cd->mcodeptr;
				M_BLE(0); 
				N_AHI(
					REG_ITMP1,
					(s4) (OFFSET(vftbl_t, interfacetable[0]) -
						superindex * sizeof(methodptr*))
				);
				M_ALD(REG_ITMP1, REG_ITMP1, 0);
				
				/* d := (REG_ITMP1 != 0)  */

				N_LTR(d, REG_ITMP1);
				M_BEQ(SZ_BRC + SZ_LHI);
				N_LHI(d, 1);

				label1 = cd->mcodeptr;

				if (super == NULL) {
					exit_label_refs[2] = cd->mcodeptr;
					M_BR(0);
				}
			}

			/* class instanceof code */

			class_label = cd->mcodeptr;

			if ((super == NULL) || !(super->flags & ACC_INTERFACE)) {
				if (super == NULL) {
					disp = dseg_add_unique_address(cd, NULL);

					codegen_add_patch_ref(cd, PATCHER_resolve_classref_to_vftbl,
										  iptr->sx.s23.s3.c.ref,
										  disp);
				}
				else {
					disp = dseg_add_address(cd, supervftbl);

					M_CLR(d);

					M_TEST(s1);
					exit_label_refs[3] = cd->mcodeptr;
					M_BEQ(0);
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
				M_ISUB(REG_ITMP3, REG_ITMP1); /* itmp1 :=  itmp1 (sub.baseval) - itmp3 (super.baseval) */

				N_CLR(REG_ITMP1, REG_ITMP2); /* d := (uint)REG_ITMP1 <= (uint)REG_ITMP2 */
				N_LHI(d, 0);
				M_BGT(SZ_BRC + SZ_LHI);
				N_LHI(d, 1);
			}

			/* resolve labels by adding the correct displacement */

			for (s2 = 0; s2 < sizeof(exit_label_refs) / sizeof(exit_label_refs[0]); ++s2) {
				if (exit_label_refs[s2])
					*(u4 *)exit_label_refs[s2] |= (u4)(cd->mcodeptr - exit_label_refs[s2]) / 2;
			}

			for (s2 = 0; s2 < sizeof(class_label_refs) / sizeof(class_label_refs[0]); ++s2) {
				if (class_label_refs[s2])
					*(u4 *)class_label_refs[s2] |= (u4)(class_label - class_label_refs[s2]) / 2;
			}

			for (s2 = 0; s2 < sizeof(label1_refs) / sizeof(label1_refs[0]); ++s2) {
				if (label1_refs[s2])
					*(u4 *)label1_refs[s2] |= (u4)(label1 - label1_refs[s2]) / 2;
			}

			emit_store_dst(jd, iptr, d);

			}

			break;

		case ICMD_MULTIANEWARRAY:/* ..., cnt1, [cnt2, ...] ==> ..., arrayref  */
			OOPS();
#if 0

			/* check for negative sizes and copy sizes to stack if necessary  */

  			MCODECHECK((10 * 4 * iptr->s1.argcount) + 5 + 10 * 8);

			for (s1 = iptr->s1.argcount; --s1 >= 0; ) {

				/* copy SAVEDVAR sizes to stack */
				var = VAR(iptr->sx.s23.s2.args[s1]);

				/* Already Preallocated? */
				if (!(var->flags & PREALLOC)) {
					s2 = emit_load(jd, iptr, var, REG_ITMP1);
					M_LST(s2, REG_SP, s1 * 8);
				}
			}

			/* is a patcher function set? */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_add_patch_ref(cd, PATCHER_builtin_multianewarray,
									  iptr->sx.s23.s3.c.ref, 0);
			}

			/* a0 = dimension count */

			M_MOV_IMM(iptr->s1.argcount, REG_A0);

			/* a1 = classinfo */

			M_MOV_IMM(iptr->sx.s23.s3.c.cls, REG_A1);

			/* a2 = pointer to dimensions = stack pointer */

			M_MOV(REG_SP, REG_A2);

			M_MOV_IMM(BUILTIN_multianewarray, REG_ITMP1);
			M_CALL(REG_ITMP1);

			/* check for exception before result assignment */

			M_TEST(REG_RESULT);
			M_BEQ(0);
			codegen_add_fillinstacktrace_ref(cd);

			s1 = codegen_reg_of_dst(jd, iptr, REG_RESULT);
			M_INTMOVE(REG_RESULT, s1);
			emit_store_dst(jd, iptr, s1);
#endif
			break;

		default:
			exceptions_throw_internalerror("Unknown ICMD %d", iptr->opc);
			return false;
	} /* switch */

	} /* for instruction */
		
	MCODECHECK(512); /* XXX require a lower number? */

	/* At the end of a basic block we may have to append some nops,
	   because the patcher stub calling code might be longer than the
	   actual instruction. So codepatching does not change the
	   following block unintentionally. */

	if (cd->mcodeptr < cd->lastmcodeptr) {
		while (cd->mcodeptr < cd->lastmcodeptr) {
			M_NOP;
		}
	}

	} /* if (bptr -> flags >= BBREACHED) */
	} /* for basic block */

	dseg_createlinenumbertable(cd);

	/* generate stubs */

	emit_exception_stubs(jd);
	emit_patcher_stubs(jd);
#if 0
	emit_replacement_stubs(jd);
#endif

	codegen_finish(jd);

	/* everything's ok */

	return true;
}


/* createcompilerstub **********************************************************

   Creates a stub routine which calls the compiler.
	
*******************************************************************************/

#define COMPILERSTUB_DATASIZE    (3 * SIZEOF_VOID_P)
#define COMPILERSTUB_CODESIZE    (SZ_AHI + SZ_L + SZ_L + SZ_BCR)

#define COMPILERSTUB_SIZE        (COMPILERSTUB_DATASIZE + COMPILERSTUB_CODESIZE)


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

	/* code for the stub */

	/* don't touch ITMP3 as it cointains the return address */

	M_ISUB_IMM((3 * 4), REG_PV); /* suppress negative displacements */

	M_ILD(REG_ITMP1, REG_PV, 1 * 4); /* methodinfo  */
	/* TODO where is methodpointer loaded into itmp2? is it already inside? */
	M_ILD(REG_PV, REG_PV, 0 * 4); /* compiler pointer */
	N_BR(REG_PV);

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

/*
           arguments on stack                   \
-------------------------------------------------| <- SP on nativestub entry
           return address                        |
           callee saved int regs (none)          |
           callee saved float regs (none)        | stack frame like in cacao
           local variable slots (none)           |
           arguments for calling methods (none) /
------------------------------------------------------------------ <- datasp 
           stackframe info
           locaref table
           integer arguments
           float arguments
96 - ...   on stack parameters (nmd->memuse slots) \ stack frame like in ABI         
0 - 96     register save area for callee           /
-------------------------------------------------------- <- SP native method
                                                            ==
                                                            SP after method entry
*/

u1 *createnativestub(functionptr f, jitdata *jd, methoddesc *nmd)
{
	methodinfo   *m;
	codeinfo     *code;
	codegendata  *cd;
	registerdata *rd;
	methoddesc   *md;
	s4            nativeparams;
	s4            i, j;                 /* count variables                    */
	s4            t;
	s4            s1, s2;
	s4            disp;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	/* initialize variables */

	md = m->parseddesc;
	nativeparams = (m->flags & ACC_STATIC) ? 2 : 1;

	/* calculate stack frame size */
#if 0
	cd->stackframesize =
		sizeof(stackframeinfo) / SIZEOF_VOID_P +
		sizeof(localref_table) / SIZEOF_VOID_P +
		INT_ARG_CNT + FLT_ARG_CNT +
		1 +                       /* functionptr, TODO: store in data segment */
		nmd->memuse;

	cd->stackframesize |= 0x1;                  /* keep stack 16-byte aligned */
#endif

	cd->stackframesize = 
		1 + /* r14 - return address */ +
		sizeof(stackframeinfo) / SIZEOF_VOID_P +
		sizeof(localref_table) / SIZEOF_VOID_P +
		1 + /* itmp3 */
		(INT_ARG_CNT + FLT_ARG_CNT) * 2 +
		nmd->memuse + /* parameter passing */
		96 / SIZEOF_VOID_P /* required by ABI */;

	cd->stackframesize |= 0x1;                  /* keep stack 8-byte aligned */


	/* create method header */

	(void) dseg_add_unique_address(cd, code);              /* CodeinfoPointer */
	(void) dseg_add_unique_s4(cd, cd->stackframesize * 4); /* FrameSize       */
	(void) dseg_add_unique_s4(cd, 0);                      /* IsSync          */
	(void) dseg_add_unique_s4(cd, 0);                      /* IsLeaf          */
	(void) dseg_add_unique_s4(cd, 0);                      /* IntSave         */
	(void) dseg_add_unique_s4(cd, 0);                      /* FltSave         */
	(void) dseg_addlinenumbertablesize(cd);
	(void) dseg_add_unique_s4(cd, 0);                      /* ExTableSize     */

	/* generate native method profiling code */
#if 0
	if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
		/* count frequency */

		M_MOV_IMM(code, REG_ITMP3);
		M_IINC_MEMBASE(REG_ITMP3, OFFSET(codeinfo, frequency));
	}
#endif

	/* generate stub code */

	N_AHI(REG_SP, -(cd->stackframesize * SIZEOF_VOID_P));

	/* save return address */

	N_ST(R14, (cd->stackframesize - 1) * SIZEOF_VOID_P, RN, REG_SP);

#if 0
#if !defined(NDEBUG)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd))
		emit_verbosecall_enter(jd);
#endif
#endif

	/* get function address (this must happen before the stackframeinfo) */

	disp = dseg_add_functionptr(cd, f);

#if !defined(WITH_STATIC_CLASSPATH)
	if (f == NULL)
		codegen_add_patch_ref(cd, PATCHER_resolve_native, m, disp);
#endif

	M_ILD(REG_ITMP1, REG_PV, disp);

	j = 96 + (nmd->memuse * 4);

	/* todo some arg registers are not volatile in C-abi terms */

	/* save integer and float argument registers */

	for (i = 0; i < md->paramcount; i++) {
		if (! md->params[i].inmemory) {
			s1 = md->params[i].regoff;

			if (IS_INT_LNG_TYPE(md->paramtypes[i].type)) {
				if (IS_2_WORD_TYPE(t)) {
					/* todo store multiple */
					N_ST(rd->argintregs[GET_HIGH_REG(s1)], j, RN, REG_SP);
					N_ST(rd->argintregs[GET_LOW_REG(s1)], j + 4, RN, REG_SP);
				} else {
					N_ST(rd->argintregs[s1], j, RN, REG_SP);
				}
			} else {
				if (IS_2_WORD_TYPE(t)) {
					N_STD(rd->argfltregs[s1], j, RN, REG_SP);
				} else {
					N_STE(rd->argfltregs[s1], j, RN, REG_SP);
				}
			}

			j += 8;
		}
	}

	N_ST(REG_ITMP1, j, RN, REG_SP);

	/* create dynamic stack info */

	N_LAE(REG_A0, (cd->stackframesize - 1) * 4, RN, REG_SP); /* datasp */
	N_LR(REG_A1, REG_PV); /* pv */
	N_LAE(REG_A2, cd->stackframesize * 4, RN, REG_SP); /* old SP */
	N_L(REG_A3, (cd->stackframesize - 1) * 4, RN, REG_SP); /* return address */

	disp = dseg_add_functionptr(cd, codegen_start_native_call);
	M_ILD(REG_ITMP1, REG_PV, disp);

	M_CALL(REG_ITMP1); /* call */

	/* restore integer and float argument registers */

	j = 96 + (nmd->memuse * 4);

	for (i = 0; i < md->paramcount; i++) {
		if (! md->params[i].inmemory) {
			s1 = md->params[i].regoff;

			if (IS_INT_LNG_TYPE(md->paramtypes[i].type)) {
				if (IS_2_WORD_TYPE(t)) {
					/* todo load multiple ! */
					N_L(rd->argintregs[GET_HIGH_REG(s1)], j, RN, REG_SP);
					N_L(rd->argintregs[GET_LOW_REG(s1)], j + 4, RN, REG_SP);
				} else {
					N_L(rd->argintregs[s1], j, RN, REG_SP);
				}
			} else {
				if (IS_2_WORD_TYPE(t)) {
					N_LD(rd->argfltregs[s1], j, RN, REG_SP);
				} else {
					N_LE(rd->argfltregs[s1], j, RN, REG_SP);
				}
			}

			j += 8;
		}
	}

	N_L(REG_ITMP1, j, RN, REG_SP);

	/* copy or spill arguments to new locations */

	for (i = md->paramcount - 1, j = i + nativeparams; i >= 0; i--, j--) {
		t = md->paramtypes[i].type;

		if (IS_INT_LNG_TYPE(t)) {

			if (!md->params[i].inmemory) {

				s1 = rd->argintregs[md->params[i].regoff];

				if (!nmd->params[j].inmemory) {
					s2 = rd->argintregs[nmd->params[j].regoff];
					if (IS_2_WORD_TYPE(t)) {
						N_LR(GET_HIGH_REG(s2), GET_HIGH_REG(s1));
						N_LR(GET_LOW_REG(s2), GET_LOW_REG(s1));
					} else {
						N_LR(s2, s1);
					}
				} else {
					s2 = nmd->params[j].regoff;
					if (IS_2_WORD_TYPE(t)) {
						N_STM(GET_LOW_REG(s1), GET_HIGH_REG(s1), 96 + (s2 * 4), REG_SP);
					} else {
						N_ST(s1, 96 + (s2 * 4), RN, REG_SP);
					}
				}

			} else {
				s1 = md->params[i].regoff + cd->stackframesize + 1;   /* + 1 (RA) */
				s2 = nmd->params[j].regoff;
				
				if (IS_2_WORD_TYPE(t)) {
					N_MVC(96 + (s2 * 4), 8, REG_SP, s1, REG_SP);
				} else {
					N_MVC(96 + (s2 * 4), 4, REG_SP, s1, REG_SP);
				}
			}

		} else {
			/* We only copy spilled float arguments, as the float argument    */
			/* registers keep unchanged.                                      */

			if (md->params[i].inmemory) {
				s1 = md->params[i].regoff + cd->stackframesize + 1;   /* + 1 (RA) */
				s2 = nmd->params[j].regoff;

				if (IS_2_WORD_TYPE(t)) {
					N_MVC(96 + (s2 * 4), 8, REG_SP, s1, REG_SP);
				} else {
					N_MVC(96 + (s2 * 4), 4, REG_SP, s1, REG_SP);
				}
			}
		}
	}

	/* put class into second argument register */

	if (m->flags & ACC_STATIC) {
		disp = dseg_add_address(cd, m->class);
		M_ILD(REG_A1, REG_PV, disp);
	}

	/* put env into first argument register */

	disp = dseg_add_address(cd, _Jv_env);
	M_ILD(REG_A0, REG_PV, disp);

	/* do the native function call */

	M_CALL(REG_ITMP1); /* call */

	/* save return value */

	t = md->returntype.type;

	if (t != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(t)) {
			if (IS_2_WORD_TYPE(t)) {
				N_STM(REG_RESULT, REG_RESULT2, 96, REG_SP);
			} else {
				N_ST(REG_RESULT, 96, RN, REG_SP);
			}
		} else {
			if (IS_2_WORD_TYPE(t)) {
				N_STD(REG_FRESULT, 96, RN, REG_SP);
			} else {
				N_STE(REG_FRESULT, 96, RN, REG_SP);
			}
		}
	}

#if 0
#if !defined(NDEBUG)
	if (JITDATA_HAS_FLAG_VERBOSECALL(jd))
		emit_verbosecall_exit(jd);
#endif
#endif

	/* remove native stackframe info */

	N_LAE(REG_A0, (cd->stackframesize - 1) * 4, RN, REG_SP); /* datasp */
	disp = dseg_add_functionptr(cd, codegen_finish_native_call);
	M_ILD(REG_ITMP1, REG_PV, disp);
	M_CALL(REG_ITMP1);
	N_LR(REG_ITMP3, REG_RESULT);

	/* restore return value */

	if (t != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(t)) {
			if (IS_2_WORD_TYPE(t)) {
				N_LM(REG_RESULT, REG_RESULT2, 96, REG_SP);
			} else {
				N_L(REG_RESULT, 96, RN, REG_SP);
			}
		} else {
			if (IS_2_WORD_TYPE(t)) {
				N_LD(REG_FRESULT, 96, RN, REG_SP);
			} else {
				N_LE(REG_FRESULT, 96, RN, REG_SP);
			}
		}
	}

	/* load return address */
	
	N_L(REG_ITMP2, (cd->stackframesize - 1) * 4, RN, REG_SP);

	/* remove stackframe */

	N_AHI(REG_SP, cd->stackframesize * 4);

	/* test for exception */

	N_LTR(REG_ITMP3, REG_ITMP3);
	N_BRC(DD_NE, SZ_BRC + SZ_BCR);

	/* return */

	N_BCR(DD_ANY, REG_ITMP2); 

	/* handle exception */

	M_MOV(REG_ITMP3, REG_ITMP1_XPTR);
	M_ALD(REG_ITMP2_XPC, REG_SP, (cd->stackframesize - 1) * 4);     /* get return address from stack */
#if 0
	/* TODO */
	M_ASUB_IMM(3, REG_ITMP2_XPC);                                    /* callq */
#endif
	disp = dseg_add_functionptr(cd, /*asm_handle_nat_exception*/ panic);
	N_AHI(REG_SP, 96);
	M_ALD(REG_ITMP3, REG_PV, disp);
	M_JMP(RN, REG_ITMP3);

	/* generate patcher stubs */

	emit_patcher_stubs(jd);

	codegen_finish(jd);

	return code->entrypoint;
	return NULL;
}

s4 codegen_reg_of_dst_notzero(jitdata *jd, instruction *iptr, s4 tempregnum) {
	codegendata *cd = jd->cd;
	s4 reg = codegen_reg_of_dst(jd, iptr, tempregnum);
	if (reg == 0) {
		M_MOV(reg, tempregnum);
		return tempregnum;
	} else {
		return reg;
	}
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
