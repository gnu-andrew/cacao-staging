/* src/vm/jit/i386/codegen.c - machine code generator for i386

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

   Changes: Joseph Wenninger
            Christian Ullrich
			Edwin Steiner

   $Id: codegen.c 5228 2006-08-09 15:11:29Z twisti $

*/


#include "config.h"

#include <assert.h>
#include <stdio.h>

#include "vm/types.h"

#include "vm/jit/i386/md-abi.h"

#include "vm/jit/i386/codegen.h"
#include "vm/jit/i386/md-emit.h"

#include "mm/memory.h"
#include "native/jni.h"
#include "native/native.h"

#if defined(ENABLE_THREADS)
# include "threads/native/lock.h"
#endif

#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/loader.h"
#include "vm/options.h"
#include "vm/stringlocal.h"
#include "vm/utf8.h"
#include "vm/vm.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/codegen-common.h"
#include "vm/jit/dseg.h"
#include "vm/jit/emit.h"
#include "vm/jit/jit.h"
#include "vm/jit/parse.h"
#include "vm/jit/patcher.h"
#include "vm/jit/reg.h"
#include "vm/jit/replace.h"

#if defined(ENABLE_LSRA)
# ifdef LSRA_USES_REG_RES
#  include "vm/jit/i386/icmd_uses_reg_res.inc"
# endif
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
	s4                  len, s1, s2, s3, d, off, disp;
	s4                  stackframesize;
	stackptr            src;
	varinfo            *var;
	basicblock         *bptr;
	instruction        *iptr;
	exceptiontable     *ex;
	u2                  currentline;
	methodinfo         *lm;             /* local methodinfo for ICMD_INVOKE*  */
	builtintable_entry *bte;
	methoddesc         *md;
	rplpoint           *replacementpoint;

	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	/* prevent compiler warnings */

	d = 0;
	currentline = 0;
	lm = NULL;
	bte = NULL;
	s2 = 0;

	{
	s4 i, p, t, l;
  	s4 savedregs_num = 0;
	s4 stack_off = 0;

	/* space to save used callee saved registers */

	savedregs_num += (INT_SAV_CNT - rd->savintreguse);

	/* float register are saved on 2 4-byte stackslots */
	savedregs_num += (FLT_SAV_CNT - rd->savfltreguse) * 2;

	stackframesize = rd->memuse + savedregs_num;

	   
#if defined(ENABLE_THREADS)
	/* space to save argument of monitor_enter */

	if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
		/* reserve 2 slots for long/double return values for monitorexit */

		if (IS_2_WORD_TYPE(m->parseddesc->returntype.type))
			stackframesize += 2;
		else
			stackframesize++;
	}
#endif

	/* create method header */

    /* Keep stack of non-leaf functions 16-byte aligned. */

	if (!jd->isleafmethod)
		stackframesize |= 0x3;

	(void) dseg_addaddress(cd, code);                      /* CodeinfoPointer */
	(void) dseg_adds4(cd, stackframesize * 4);             /* FrameSize       */

#if defined(ENABLE_THREADS)
	/* IsSync contains the offset relative to the stack pointer for the
	   argument of monitor_exit used in the exception handler. Since the
	   offset could be zero and give a wrong meaning of the flag it is
	   offset by one.
	*/

	if (checksync && (m->flags & ACC_SYNCHRONIZED))
		(void) dseg_adds4(cd, (rd->memuse + 1) * 4);       /* IsSync          */
	else
#endif
		(void) dseg_adds4(cd, 0);                          /* IsSync          */
	                                       
	(void) dseg_adds4(cd, jd->isleafmethod);               /* IsLeaf          */
	(void) dseg_adds4(cd, INT_SAV_CNT - rd->savintreguse); /* IntSave         */
	(void) dseg_adds4(cd, FLT_SAV_CNT - rd->savfltreguse); /* FltSave         */

	/* adds a reference for the length of the line number counter. We don't
	   know the size yet, since we evaluate the information during code
	   generation, to save one additional iteration over the whole
	   instructions. During code optimization the position could have changed
	   to the information gotten from the class file */
	(void) dseg_addlinenumbertablesize(cd);

	(void) dseg_adds4(cd, cd->exceptiontablelength);       /* ExTableSize     */
	
	/* create exception table */

	for (ex = cd->exceptiontable; ex != NULL; ex = ex->down) {
		dseg_addtarget(cd, ex->start);
   		dseg_addtarget(cd, ex->end);
		dseg_addtarget(cd, ex->handler);
		(void) dseg_addaddress(cd, ex->catchtype.cls);
	}
	
	/* generate method profiling code */

	if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
		/* count frequency */

		M_MOV_IMM(code, REG_ITMP3);
		M_IADD_IMM_MEMBASE(1, REG_ITMP3, OFFSET(codeinfo, frequency));
	}

	/* create stack frame (if necessary) */

	if (stackframesize)
		M_ASUB_IMM(stackframesize * 4, REG_SP);

	/* save return address and used callee saved registers */

  	p = stackframesize;
	for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
 		p--; M_AST(rd->savintregs[i], REG_SP, p * 4);
	}
	for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
		p-=2; emit_fld_reg(cd, rd->savfltregs[i]); emit_fstpl_membase(cd, REG_SP, p * 4);
	}

	/* take arguments out of register or stack frame */

	md = m->parseddesc;

	stack_off = 0;
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
			if (!md->params[p].inmemory) {           /* register arguments    */
				log_text("integer register argument");
				assert(0);
				if (!(var->flags & INMEMORY)) {      /* reg arg -> register   */
					/* rd->argintregs[md->params[p].regoff -> var->regoff     */
				} else {                             /* reg arg -> spilled    */
					/* rd->argintregs[md->params[p].regoff -> var->regoff * 4 */
				}
			} else {                                 /* stack arguments       */
				if (!(var->flags & INMEMORY)) {      /* stack arg -> register */
					emit_mov_membase_reg(           /* + 4 for return address */
					   cd, REG_SP, (stackframesize + s1) * 4 + 4, var->regoff);
					                                /* + 4 for return address */
				} else {                             /* stack arg -> spilled  */
					if (!IS_2_WORD_TYPE(t)) {
#if 0
						emit_mov_membase_reg(       /* + 4 for return address */
					         cd, REG_SP, (stackframesize + s1) * 4 + 4,
							 REG_ITMP1);    
						emit_mov_reg_membase(
						    cd, REG_ITMP1, REG_SP, var->regoff * 4);
#else
						                  /* reuse Stackslotand avoid copying */
						var->regoff = stackframesize + s1 + 1;
#endif

					} else {
#if 0
						emit_mov_membase_reg(       /* + 4 for return address */
						    cd, REG_SP, (stackframesize + s1) * 4 + 4,
							REG_ITMP1);
						emit_mov_reg_membase(
						    cd, REG_ITMP1, REG_SP, var->regoff * 4);
						emit_mov_membase_reg(       /* + 4 for return address */
                            cd, REG_SP, (stackframesize + s1) * 4 + 4 + 4,
                            REG_ITMP1);             
						emit_mov_reg_membase(
					        cd, REG_ITMP1, REG_SP, var->regoff * 4 + 4);
#else
						                  /* reuse Stackslotand avoid copying */
						var->regoff = stackframesize + s1 + 1;
#endif
					}
				}
			}
		
		} else {                                     /* floating args         */
			if (!md->params[p].inmemory) {           /* register arguments    */
				log_text("There are no float argument registers!");
				assert(0);
				if (!(var->flags & INMEMORY)) {  /* reg arg -> register   */
					/* rd->argfltregs[md->params[p].regoff -> var->regoff     */
				} else {			             /* reg arg -> spilled    */
					/* rd->argfltregs[md->params[p].regoff -> var->regoff * 4 */
				}

			} else {                                 /* stack arguments       */
				if (!(var->flags & INMEMORY)) {      /* stack-arg -> register */
					if (t == TYPE_FLT) {
						emit_flds_membase(
                            cd, REG_SP, (stackframesize + s1) * 4 + 4);
						assert(0);
/* 						emit_fstp_reg(cd, var->regoff + fpu_st_offset); */

					} else {
						emit_fldl_membase(
                            cd, REG_SP, (stackframesize + s1) * 4 + 4);
						assert(0);
/* 						emit_fstp_reg(cd, var->regoff + fpu_st_offset); */
					}

				} else {                             /* stack-arg -> spilled  */
#if 0
					emit_mov_membase_reg(
                        cd, REG_SP, (stackframesize + s1) * 4 + 4, REG_ITMP1);
					emit_mov_reg_membase(
					    cd, REG_ITMP1, REG_SP, var->regoff * 4);
					if (t == TYPE_FLT) {
						emit_flds_membase(
						    cd, REG_SP, (stackframesize + s1) * 4 + 4);
						emit_fstps_membase(cd, REG_SP, var->regoff * 4);
					} else {
						emit_fldl_membase(
                            cd, REG_SP, (stackframesize + s1) * 4 + 4);
						emit_fstpl_membase(cd, REG_SP, var->regoff * 4);
					}
#else
						                  /* reuse Stackslotand avoid copying */
						var->regoff = stackframesize + s1 + 1;
#endif
				}
			}
		}
	}  /* end for */

	/* call monitorenter function */

#if defined(ENABLE_THREADS)
	if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
		s1 = rd->memuse;

		if (m->flags & ACC_STATIC) {
			M_MOV_IMM(&m->class->object.header, REG_ITMP1);
		}
		else {
			M_ALD(REG_ITMP1, REG_SP, stackframesize * 4 + 4);
			M_TEST(REG_ITMP1);
			M_BEQ(0);
			codegen_add_nullpointerexception_ref(cd);
		}

		M_AST(REG_ITMP1, REG_SP, s1 * 4);
		M_AST(REG_ITMP1, REG_SP, 0 * 4);
		M_MOV_IMM(LOCK_monitor_enter, REG_ITMP3);
		M_CALL(REG_ITMP3);
	}			
#endif

	/* copy argument registers to stack and call trace function with pointer
	   to arguments on stack.
	*/

#if !defined(NDEBUG)
	if (opt_verbosecall) {
		stack_off = 0;
		s1 = INT_TMP_CNT * 4 + TRACE_ARGS_NUM * 8 + 4 + 4 + stackframesize * 4;

		M_ISUB_IMM(INT_TMP_CNT * 4 + TRACE_ARGS_NUM * 8 + 4, REG_SP);

		/* save temporary registers for leaf methods */

		for (p = 0; p < INT_TMP_CNT; p++)
			M_IST(rd->tmpintregs[p], REG_SP, TRACE_ARGS_NUM * 8 + 4 + p * 4);

		for (p = 0, l = 0; p < md->paramcount && p < TRACE_ARGS_NUM; p++) {
			t = md->paramtypes[p].type;

			if (IS_INT_LNG_TYPE(t)) {
				if (IS_2_WORD_TYPE(t)) {
					emit_mov_membase_reg(cd, REG_SP, s1 + stack_off, REG_ITMP1);
					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8);
					emit_mov_membase_reg(cd, REG_SP, s1 + stack_off + 4, REG_ITMP1);
					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8 + 4);

 				} else if (t == TYPE_ADR) {
/* 				} else { */
					emit_mov_membase_reg(cd, REG_SP, s1 + stack_off, REG_ITMP1);
					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8);
					emit_alu_reg_reg(cd, ALU_XOR, REG_ITMP1, REG_ITMP1);
					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8 + 4);

 				} else {
 					emit_mov_membase_reg(cd, REG_SP, s1 + stack_off, EAX);
 					emit_cltd(cd);
 					emit_mov_reg_membase(cd, EAX, REG_SP, p * 8);
 					emit_mov_reg_membase(cd, EDX, REG_SP, p * 8 + 4);
				}

			} else {
				if (!IS_2_WORD_TYPE(t)) {
					emit_flds_membase(cd, REG_SP, s1 + stack_off);
					emit_fstps_membase(cd, REG_SP, p * 8);
					emit_alu_reg_reg(cd, ALU_XOR, REG_ITMP1, REG_ITMP1);
					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8 + 4);

				} else {
					emit_fldl_membase(cd, REG_SP, s1 + stack_off);
					emit_fstpl_membase(cd, REG_SP, p * 8);
				}
			}
			stack_off += (IS_2_WORD_TYPE(t)) ? 8 : 4;
		}

		/* fill up the remaining arguments */
		emit_alu_reg_reg(cd, ALU_XOR, REG_ITMP1, REG_ITMP1);
		for (p = md->paramcount; p < TRACE_ARGS_NUM; p++) {
			emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8);
			emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, p * 8 + 4);
		}

		emit_mov_imm_membase(cd, (ptrint) m, REG_SP, TRACE_ARGS_NUM * 8);
  		emit_mov_imm_reg(cd, (ptrint) builtin_trace_args, REG_ITMP1);
		emit_call_reg(cd, REG_ITMP1);

		/* restore temporary registers for leaf methods */

		for (p = 0; p < INT_TMP_CNT; p++)
			M_ILD(rd->tmpintregs[p], REG_SP, TRACE_ARGS_NUM * 8 + 4 + p * 4);

		M_IADD_IMM(INT_TMP_CNT * 4 + TRACE_ARGS_NUM * 8 + 4, REG_SP);
	}
#endif /* !defined(NDEBUG) */

	}

	/* end of header generation */

	replacementpoint = jd->code->rplpoints;

	/* walk through all basic blocks */
	for (bptr = m->basicblocks; bptr != NULL; bptr = bptr->next) {

		bptr->mpc = (s4) (cd->mcodeptr - cd->mcodebase);

		if (bptr->flags >= BBREACHED) {

		/* branch resolving */

		branchref *brefs;
		for (brefs = bptr->branchrefs; brefs != NULL; brefs = brefs->next) {
			gen_resolvebranch(cd->mcodebase + brefs->branchpos, 
			                  brefs->branchpos,
							  bptr->mpc);
		}

		/* handle replacement points */

		if (bptr->bitflags & BBFLAG_REPLACEMENT) {
			replacementpoint->pc = (u1*)bptr->mpc; /* will be resolved later */
			
			replacementpoint++;

			assert(cd->lastmcodeptr <= cd->mcodeptr);
			cd->lastmcodeptr = cd->mcodeptr + 5; /* 5 byte jmp patch */
		}

		/* copy interface registers to their destination */

		src = bptr->instack;
		len = bptr->indepth;
		MCODECHECK(512);

#if 0
		/* generate basic block profiling code */

		if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
			/* count frequency */

			M_MOV_IMM(code->bbfrequency, REG_ITMP3);
			M_IADD_IMM_MEMBASE(1, REG_ITMP3, bptr->nr * 4);
		}
#endif

#if defined(ENABLE_LSRA)
		if (opt_lsra) {
			while (src != NULL) {
				len--;
				if ((len == 0) && (bptr->type != BBTYPE_STD)) {
					if (!IS_2_WORD_TYPE(src->type)) {
						if (bptr->type == BBTYPE_SBR) {
							/* 							d = reg_of_var(m, src, REG_ITMP1); */
							if (!(src->flags & INMEMORY))
								d = src->regoff;
							else
								d = REG_ITMP1;

							emit_pop_reg(cd, d);
							emit_store(jd, NULL, src, d);

						} else if (bptr->type == BBTYPE_EXH) {
							/* 							d = reg_of_var(m, src, REG_ITMP1); */
							if (!(src->flags & INMEMORY))
								d = src->regoff;
							else
								d = REG_ITMP1;
							M_INTMOVE(REG_ITMP1, d);
							emit_store(jd, NULL, src, d);
						}

					} else {
						log_text("copy interface registers(EXH, SBR): longs have to be in memory (begin 1)");
						assert(0);
					}
				}
				src = src->prev;
			}

		} else {
#endif
		while (src != NULL) {
			len--;
			if ((len == bptr->indepth-1) && (bptr->type != BBTYPE_STD)) {
				if (!IS_2_WORD_TYPE(src->type)) {
					if (bptr->type == BBTYPE_SBR) {
						d = codegen_reg_of_var(rd, 0, src, REG_ITMP1);
						emit_pop_reg(cd, d);
						emit_store(jd, NULL, src, d);
					} else if (bptr->type == BBTYPE_EXH) {
						d = codegen_reg_of_var(rd, 0, src, REG_ITMP1);
						M_INTMOVE(REG_ITMP1, d);
						emit_store(jd, NULL, src, d);
					}

				} else {
					log_text("copy interface registers: longs have to be in memory (begin 1)");
					assert(0);
				}

			} else {
				if (IS_LNG_TYPE(src->type))
					d = codegen_reg_of_var(rd, 0, src, PACK_REGS(REG_ITMP1, REG_ITMP2));
				else
					d = codegen_reg_of_var(rd, 0, src, REG_ITMP1);
/* 					d = codegen_reg_of_var(rd, 0, src, REG_IFTMP); */

				if ((src->varkind != STACKVAR)) {
					s2 = src->type;
					s1 = rd->interfaces[len][s2].regoff;

					if (IS_FLT_DBL_TYPE(s2)) {
						if (!(rd->interfaces[len][s2].flags & INMEMORY)) {
							M_FLTMOVE(s1, d);

						} else {
							if (IS_2_WORD_TYPE(s2))
								M_DLD(d, REG_SP, s1 * 4);
							else
								M_FLD(d, REG_SP, s1 * 4);
						}

					} else {
						if (!(rd->interfaces[len][s2].flags & INMEMORY)) {
							if (IS_2_WORD_TYPE(s2))
								M_LNGMOVE(s1, d);
							else
								M_INTMOVE(s1, d);

						} else {
							if (IS_2_WORD_TYPE(s2))
								M_LLD(d, REG_SP, s1 * 4);
							else
								M_ILD(d, REG_SP, s1 * 4);
						}
					}

					emit_store(jd, NULL, src, d);
				}
			}
			src = src->prev;
		}
#if defined(ENABLE_LSRA)
		}
#endif

		/* walk through all instructions */
		
		src = bptr->instack;
		len = bptr->icount;
		currentline = 0;
		for (iptr = bptr->iinstr; len > 0; src = iptr->dst, len--, iptr++) {
			if (iptr->line != currentline) {
				dseg_addlinenumber(cd, iptr->line);
				currentline = iptr->line;
			}

			MCODECHECK(1024);                         /* 1kB should be enough */

		switch (iptr->opc) {
		case ICMD_INLINE_START:
			{
				insinfo_inline *insinfo = (insinfo_inline *) iptr->target;
#if defined(ENABLE_THREADS)
				if (insinfo->synchronize) {
					/* add monitor enter code */
					if (insinfo->method->flags & ACC_STATIC) {
						M_MOV_IMM(&insinfo->method->class->object.header, REG_ITMP1);
						M_AST(REG_ITMP1, REG_SP, 0 * 4);
					} 
					else {
						/* nullpointer check must have been performed before */
						/* (XXX not done, yet) */
						var = &(rd->locals[insinfo->synclocal][TYPE_ADR]);
						if (var->flags & INMEMORY) {
							emit_mov_membase_reg(cd, REG_SP, var->regoff * 4, REG_ITMP1);
							M_AST(REG_ITMP1, REG_SP, 0 * 4);
						} 
						else {
							M_AST(var->regoff, REG_SP, 0 * 4);
						}
					}

					M_MOV_IMM(LOCK_monitor_enter, REG_ITMP3);
					M_CALL(REG_ITMP3);
				}
#endif
				dseg_addlinenumber_inline_start(cd, iptr);
			}
			break;

		case ICMD_INLINE_END:
			{
				insinfo_inline *insinfo = (insinfo_inline *) iptr->target;

				dseg_addlinenumber_inline_end(cd, iptr);
				dseg_addlinenumber(cd, iptr->line);

#if defined(ENABLE_THREADS)
				if (insinfo->synchronize) {
					/* add monitor exit code */
					if (insinfo->method->flags & ACC_STATIC) {
						M_MOV_IMM(&insinfo->method->class->object.header, REG_ITMP1);
						M_AST(REG_ITMP1, REG_SP, 0 * 4);
					} 
					else {
						var = &(rd->locals[insinfo->synclocal][TYPE_ADR]);
						if (var->flags & INMEMORY) {
							M_ALD(REG_ITMP1, REG_SP, var->regoff * 4);
							M_AST(REG_ITMP1, REG_SP, 0 * 4);
						} 
						else {
							M_AST(var->regoff, REG_SP, 0 * 4);
						}
					}

					M_MOV_IMM(LOCK_monitor_exit, REG_ITMP3);
					M_CALL(REG_ITMP3);
				}
#endif
			}
			break;

		case ICMD_NOP:        /* ...  ==> ...                                 */
			break;

		case ICMD_CHECKNULL:  /* ..., objectref  ==> ..., objectref           */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_TEST(s1);
			M_BEQ(0);
			codegen_add_nullpointerexception_ref(cd);
			break;

		/* constant operations ************************************************/

		case ICMD_ICONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.i = constant                    */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			ICONST(d, iptr->val.i);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.l = constant                    */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			LCONST(d, iptr->val.l);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.f = constant                    */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
			if (iptr->val.f == 0.0) {
				emit_fldz(cd);

				/* -0.0 */
				if (iptr->val.i == 0x80000000) {
					emit_fchs(cd);
				}

			} else if (iptr->val.f == 1.0) {
				emit_fld1(cd);

			} else if (iptr->val.f == 2.0) {
				emit_fld1(cd);
				emit_fld1(cd);
				emit_faddp(cd);

			} else {
  				disp = dseg_addfloat(cd, iptr->val.f);
				emit_mov_imm_reg(cd, 0, REG_ITMP1);
				dseg_adddata(cd);
				emit_flds_membase(cd, REG_ITMP1, disp);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;
		
		case ICMD_DCONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.d = constant                    */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
			if (iptr->val.d == 0.0) {
				emit_fldz(cd);

				/* -0.0 */
				if (iptr->val.l == 0x8000000000000000LL) {
					emit_fchs(cd);
				}

			} else if (iptr->val.d == 1.0) {
				emit_fld1(cd);

			} else if (iptr->val.d == 2.0) {
				emit_fld1(cd);
				emit_fld1(cd);
				emit_faddp(cd);

			} else {
				disp = dseg_adddouble(cd, iptr->val.d);
				emit_mov_imm_reg(cd, 0, REG_ITMP1);
				dseg_adddata(cd);
				emit_fldl_membase(cd, REG_ITMP1, disp);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ACONST:     /* ...  ==> ..., constant                       */
		                      /* op1 = 0, val.a = constant                    */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_aconst,
									ICMD_ACONST_UNRESOLVED_CLASSREF(iptr), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				M_MOV_IMM(NULL, d);

			} else {
				if (iptr->val.a == NULL)
					M_CLR(d);
				else
					M_MOV_IMM(iptr->val.a, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;


		/* load/store operations **********************************************/

		case ICMD_ILOAD:      /* ...  ==> ..., content of local variable      */
		case ICMD_ALOAD:      /* op1 = local variable                         */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if ((iptr->dst->varkind == LOCALVAR) &&
			    (iptr->dst->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (var->flags & INMEMORY)
				M_ILD(d, REG_SP, var->regoff * 4);
			else
				M_INTMOVE(var->regoff, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */
  
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			if ((iptr->dst->varkind == LOCALVAR) &&
			    (iptr->dst->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (var->flags & INMEMORY)
				M_LLD(d, REG_SP, var->regoff * 4);
			else
				M_LNGMOVE(var->regoff, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
  			if ((iptr->dst->varkind == LOCALVAR) &&
  			    (iptr->dst->varnum == iptr->op1))
    				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (var->flags & INMEMORY)
				M_FLD(d, REG_SP, var->regoff * 4);
			else
				M_FLTMOVE(var->regoff, d);
  			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DLOAD:      /* ...  ==> ..., content of local variable      */
		                      /* op1 = local variable                         */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
  			if ((iptr->dst->varkind == LOCALVAR) &&
  			    (iptr->dst->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ILOAD]);
			if (var->flags & INMEMORY)
				M_DLD(d, REG_SP, var->regoff * 4);
			else
				M_FLTMOVE(var->regoff, d);
  			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISTORE:     /* ..., value  ==> ...                          */
		case ICMD_ASTORE:     /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
				M_IST(s1, REG_SP, var->regoff * 4);	
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, var->regoff);
				M_INTMOVE(s1, var->regoff);
			}
			break;

		case ICMD_LSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
				M_LST(s1, REG_SP, var->regoff * 4);
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, var->regoff);
				M_LNGMOVE(s1, var->regoff);
			}
			break;

		case ICMD_FSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
				M_FST(s1, REG_SP, var->regoff * 4);
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, var->regoff);
				M_FLTMOVE(s1, var->regoff);
			}
			break;

		case ICMD_DSTORE:     /* ..., value  ==> ...                          */
		                      /* op1 = local variable                         */

			if ((src->varkind == LOCALVAR) &&
			    (src->varnum == iptr->op1))
				break;
			var = &(rd->locals[iptr->op1][iptr->opc - ICMD_ISTORE]);
			if (var->flags & INMEMORY) {
				s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
				M_DST(s1, REG_SP, var->regoff * 4);
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, var->regoff);
				M_FLTMOVE(s1, var->regoff);
			}
			break;


		/* pop/dup/swap operations ********************************************/

		/* attention: double and longs are only one entry in CACAO ICMDs      */

		case ICMD_POP:        /* ..., value  ==> ...                          */
		case ICMD_POP2:       /* ..., value, value  ==> ...                   */
			break;

		case ICMD_DUP:        /* ..., a ==> ..., a, a                         */

			M_COPY(src, iptr->dst);
			break;

		case ICMD_DUP2:       /* ..., a, b ==> ..., a, b, a, b                */

			M_COPY(src,       iptr->dst);
			M_COPY(src->prev, iptr->dst->prev);
			break;

		case ICMD_DUP_X1:     /* ..., a, b ==> ..., b, a, b                   */

			M_COPY(src,       iptr->dst);
			M_COPY(src->prev, iptr->dst->prev);
			M_COPY(iptr->dst, iptr->dst->prev->prev);
			break;

		case ICMD_DUP_X2:     /* ..., a, b, c ==> ..., c, a, b, c             */

			M_COPY(src,             iptr->dst);
			M_COPY(src->prev,       iptr->dst->prev);
			M_COPY(src->prev->prev, iptr->dst->prev->prev);
			M_COPY(iptr->dst,       iptr->dst->prev->prev->prev);
			break;

		case ICMD_DUP2_X1:    /* ..., a, b, c ==> ..., b, c, a, b, c          */

			M_COPY(src,             iptr->dst);
			M_COPY(src->prev,       iptr->dst->prev);
			M_COPY(src->prev->prev, iptr->dst->prev->prev);
			M_COPY(iptr->dst,       iptr->dst->prev->prev->prev);
			M_COPY(iptr->dst->prev, iptr->dst->prev->prev->prev->prev);
			break;

		case ICMD_DUP2_X2:    /* ..., a, b, c, d ==> ..., c, d, a, b, c, d    */

			M_COPY(src,                   iptr->dst);
			M_COPY(src->prev,             iptr->dst->prev);
			M_COPY(src->prev->prev,       iptr->dst->prev->prev);
			M_COPY(src->prev->prev->prev, iptr->dst->prev->prev->prev);
			M_COPY(iptr->dst,             iptr->dst->prev->prev->prev->prev);
			M_COPY(iptr->dst->prev,       iptr->dst->prev->prev->prev->prev->prev);
			break;

		case ICMD_SWAP:       /* ..., a, b ==> ..., b, a                      */

			M_COPY(src,       iptr->dst->prev);
			M_COPY(src->prev, iptr->dst);
			break;


		/* integer operations *************************************************/

		case ICMD_INEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1); 
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_NEG(d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_NEG(GET_LOW_REG(d));
			M_IADDC_IMM(0, GET_HIGH_REG(d));
			M_NEG(GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_I2L:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, src, EAX);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, EAX_EDX_PACKED);
			M_INTMOVE(s1, EAX);
			M_CLTD;
			M_LNGMOVE(EAX_EDX_PACKED, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_L2I:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			M_INTMOVE(s1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_INT2BYTE:   /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_SLL_IMM(24, d);
			M_SRA_IMM(24, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_INT2CHAR:   /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			M_CZEXT(s1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_INT2SHORT:  /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			M_SSEXT(s1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;


		case ICMD_IADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s2 == d)
				M_IADD(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IADD(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_IADD_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_INTMOVE(s1, GET_LOW_REG(d));
			M_IADD(s2, GET_LOW_REG(d));
			/* don't use REG_ITMP1 */
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			M_INTMOVE(s1, GET_HIGH_REG(d));
			M_IADDC(s2, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_IADD_IMM(iptr->val.l, GET_LOW_REG(d));
			M_IADDC_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (s2 == d) {
				M_INTMOVE(s1, REG_ITMP1);
				M_ISUB(s2, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, d);
			}
			else {
				M_INTMOVE(s1, d);
				M_ISUB(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISUBCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_ISUB_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			if (s2 == GET_LOW_REG(d)) {
				M_INTMOVE(s1, REG_ITMP1);
				M_ISUB(s2, REG_ITMP1);
				M_INTMOVE(REG_ITMP1, GET_LOW_REG(d));
			}
			else {
				M_INTMOVE(s1, GET_LOW_REG(d));
				M_ISUB(s2, GET_LOW_REG(d));
			}
			/* don't use REG_ITMP1 */
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			if (s2 == GET_HIGH_REG(d)) {
				M_INTMOVE(s1, REG_ITMP2);
				M_ISUBB(s2, REG_ITMP2);
				M_INTMOVE(REG_ITMP2, GET_HIGH_REG(d));
			}
			else {
				M_INTMOVE(s1, GET_HIGH_REG(d));
				M_ISUBB(s2, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LSUBCONST:  /* ..., value  ==> ..., value - constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_ISUB_IMM(iptr->val.l, GET_LOW_REG(d));
			M_ISUBB_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s2 == d)
				M_IMUL(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_IMUL(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			M_IMUL_IMM(s1, iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_low(jd, iptr, src, EDX);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, EAX_EDX_PACKED);

			M_INTMOVE(s1, REG_ITMP2);
			M_IMUL(s2, REG_ITMP2);

			s1 = emit_load_s1_low(jd, iptr, src->prev, EAX);
			s2 = emit_load_s2_high(jd, iptr, src, EDX);
			M_INTMOVE(s2, EDX);
			M_IMUL(s1, EDX);
			M_IADD(EDX, REG_ITMP2);

			s1 = emit_load_s1_low(jd, iptr, src->prev, EAX);
			s2 = emit_load_s2_low(jd, iptr, src, EDX);
			M_INTMOVE(s1, EAX);
			M_MUL(s2);
			M_INTMOVE(EAX, GET_LOW_REG(d));
			M_IADD(REG_ITMP2, GET_HIGH_REG(d));

			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, EAX_EDX_PACKED);
			ICONST(EAX, iptr->val.l);
			M_MUL(s1);
			M_IMUL_IMM(s1, iptr->val.l >> 32, REG_ITMP2);
			M_IADD(REG_ITMP2, EDX);
			s1 = emit_load_s1_high(jd, iptr, src, REG_ITMP2);
			M_IMUL_IMM(s1, iptr->val.l, REG_ITMP2);
			M_IADD(REG_ITMP2, EDX);
			M_LNGMOVE(EAX_EDX_PACKED, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, EAX);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, EAX);

			if (checknull) {
				M_TEST(s2);
				M_BEQ(0);
				codegen_add_arithmeticexception_ref(cd);
			}

			M_INTMOVE(s1, EAX);           /* we need the first operand in EAX */

			/* check as described in jvm spec */

			M_CMP_IMM(0x80000000, EAX);
			M_BNE(3 + 6);
			M_CMP_IMM(-1, s2);
			M_BEQ(1 + 2);
  			M_CLTD;
			M_IDIV(s2);

			M_INTMOVE(EAX, d);           /* if INMEMORY then d is already EAX */
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, EAX);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, EDX);

			if (checknull) {
				M_TEST(s2);
				M_BEQ(0);
				codegen_add_arithmeticexception_ref(cd);
			}

			M_INTMOVE(s1, EAX);           /* we need the first operand in EAX */

			/* check as described in jvm spec */

			M_CMP_IMM(0x80000000, EAX);
			M_BNE(2 + 3 + 6);
			M_CLR(EDX);
			M_CMP_IMM(-1, s2);
			M_BEQ(1 + 2);
  			M_CLTD;
			M_IDIV(s2);

			M_INTMOVE(EDX, d);           /* if INMEMORY then d is already EDX */
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			/* TODO: optimize for `/ 2' */
			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_TEST(d);
			M_BNS(6);
			M_IADD_IMM32((1 << iptr->val.i) - 1, d);  /* 32-bit for jump off. */
			M_SRA_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s1 == d) {
				M_MOV(s1, REG_ITMP1);
				s1 = REG_ITMP1;
			} 
			M_INTMOVE(s1, d);
			M_AND_IMM(iptr->val.i, d);
			M_TEST(s1);
			M_BGE(2 + 2 + 6 + 2);
			M_MOV(s1, d);  /* don't use M_INTMOVE, so we know the jump offset */
			M_NEG(d);
			M_AND_IMM32(iptr->val.i, d);        /* use 32-bit for jump offset */
			M_NEG(d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
		case ICMD_LREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			s2 = emit_load_s2(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT_PACKED);

			M_INTMOVE(GET_LOW_REG(s2), REG_ITMP3);
			M_OR(GET_HIGH_REG(s2), REG_ITMP3);
			M_BEQ(0);
			codegen_add_arithmeticexception_ref(cd);

			bte = iptr->val.a;
			md = bte->md;

			M_LST(s2, REG_SP, 2 * 4);

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP12_PACKED);
			M_LST(s1, REG_SP, 0 * 4);

			M_MOV_IMM(bte->fp, REG_ITMP3);
			M_CALL(REG_ITMP3);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LDIVPOW2:   /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT_PACKED);
			M_LNGMOVE(s1, d);
			M_TEST(GET_HIGH_REG(d));
			M_BNS(6 + 3);
			M_IADD_IMM32((1 << iptr->val.i) - 1, GET_LOW_REG(d));
			M_IADDC_IMM(0, GET_HIGH_REG(d));
			M_SRLD_IMM(iptr->val.i, GET_HIGH_REG(d), GET_LOW_REG(d));
			M_SRA_IMM(iptr->val.i, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

#if 0
		case ICMD_LREMPOW2:   /* ..., value  ==> ..., value % constant        */
		                      /* val.l = constant                             */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);
			if (iptr->dst->flags & INMEMORY) {
				if (src->flags & INMEMORY) {
					/* Alpha algorithm */
					disp = 3;
					CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
					disp += 3;
					CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4 + 4);

					disp += 2;
					disp += 3;
					disp += 2;

					/* TODO: hmm, don't know if this is always correct */
					disp += 2;
					CALCIMMEDIATEBYTES(disp, iptr->val.l & 0x00000000ffffffff);
					disp += 2;
					CALCIMMEDIATEBYTES(disp, iptr->val.l >> 32);

					disp += 2;
					disp += 3;
					disp += 2;

					emit_mov_membase_reg(cd, REG_SP, src->regoff * 4, REG_ITMP1);
					emit_mov_membase_reg(cd, REG_SP, src->regoff * 4 + 4, REG_ITMP2);
					
					emit_alu_imm_reg(cd, ALU_AND, iptr->val.l, REG_ITMP1);
					emit_alu_imm_reg(cd, ALU_AND, iptr->val.l >> 32, REG_ITMP2);
					emit_alu_imm_membase(cd, ALU_CMP, 0, REG_SP, src->regoff * 4 + 4);
					emit_jcc(cd, CC_GE, disp);

					emit_mov_membase_reg(cd, REG_SP, src->regoff * 4, REG_ITMP1);
					emit_mov_membase_reg(cd, REG_SP, src->regoff * 4 + 4, REG_ITMP2);
					
					emit_neg_reg(cd, REG_ITMP1);
					emit_alu_imm_reg(cd, ALU_ADC, 0, REG_ITMP2);
					emit_neg_reg(cd, REG_ITMP2);
					
					emit_alu_imm_reg(cd, ALU_AND, iptr->val.l, REG_ITMP1);
					emit_alu_imm_reg(cd, ALU_AND, iptr->val.l >> 32, REG_ITMP2);
					
					emit_neg_reg(cd, REG_ITMP1);
					emit_alu_imm_reg(cd, ALU_ADC, 0, REG_ITMP2);
					emit_neg_reg(cd, REG_ITMP2);

					emit_mov_reg_membase(cd, REG_ITMP1, REG_SP, iptr->dst->regoff * 4);
					emit_mov_reg_membase(cd, REG_ITMP2, REG_SP, iptr->dst->regoff * 4 + 4);
				}
			}

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT_PACKED);
			M_LNGMOVE(s1, d);
			M_AND_IMM(iptr->val.l, GET_LOW_REG(d));	
			M_AND_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			M_TEST(GET_LOW_REG(s1));
			M_BGE(0);
			M_LNGMOVE(s1, d);
		break;
#endif

		case ICMD_ISHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s2, ECX);                       /* s2 may be equal to d */
			M_INTMOVE(s1, d);
			M_SLL(d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISHLCONST:  /* ..., value  ==> ..., value << constant       */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_SLL_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s2, ECX);                       /* s2 may be equal to d */
			M_INTMOVE(s1, d);
			M_SRA(d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_ISHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_SRA_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s2, ECX);                       /* s2 may be equal to d */
			M_INTMOVE(s1, d);
			M_SRL(d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_SRL_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LSHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP13_PACKED);
			s2 = emit_load_s2(jd, iptr, src, ECX);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP13_PACKED);
			M_LNGMOVE(s1, d);
			M_INTMOVE(s2, ECX);
			M_TEST_IMM(32, ECX);
			M_BEQ(2 + 2);
			M_MOV(GET_LOW_REG(d), GET_HIGH_REG(d));
			M_CLR(GET_LOW_REG(d));
			M_SLLD(GET_LOW_REG(d), GET_HIGH_REG(d));
			M_SLL(GET_LOW_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

        case ICMD_LSHLCONST:  /* ..., value  ==> ..., value << constant       */
 			                  /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			if (iptr->val.i & 0x20) {
				M_MOV(GET_LOW_REG(d), GET_HIGH_REG(d));
				M_CLR(GET_LOW_REG(d));
				M_SLLD_IMM(iptr->val.i & 0x3f, GET_LOW_REG(d), GET_HIGH_REG(d));
			}
			else {
				M_SLLD_IMM(iptr->val.i & 0x3f, GET_LOW_REG(d), GET_HIGH_REG(d));
				M_SLL_IMM(iptr->val.i & 0x3f, GET_LOW_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LSHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP13_PACKED);
			s2 = emit_load_s2(jd, iptr, src, ECX);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP13_PACKED);
			M_LNGMOVE(s1, d);
			M_INTMOVE(s2, ECX);
			M_TEST_IMM(32, ECX);
			M_BEQ(2 + 3);
			M_MOV(GET_HIGH_REG(d), GET_LOW_REG(d));
			M_SRA_IMM(31, GET_HIGH_REG(d));
			M_SRLD(GET_HIGH_REG(d), GET_LOW_REG(d));
			M_SRA(GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LSHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			if (iptr->val.i & 0x20) {
				M_MOV(GET_HIGH_REG(d), GET_LOW_REG(d));
				M_SRA_IMM(31, GET_HIGH_REG(d));
				M_SRLD_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d), GET_LOW_REG(d));
			}
			else {
				M_SRLD_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d), GET_LOW_REG(d));
				M_SRA_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP13_PACKED);
			s2 = emit_load_s2(jd, iptr, src, ECX);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP13_PACKED);
			M_LNGMOVE(s1, d);
			M_INTMOVE(s2, ECX);
			M_TEST_IMM(32, ECX);
			M_BEQ(2 + 2);
			M_MOV(GET_HIGH_REG(d), GET_LOW_REG(d));
			M_CLR(GET_HIGH_REG(d));
			M_SRLD(GET_HIGH_REG(d), GET_LOW_REG(d));
			M_SRL(GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

  		case ICMD_LUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
  		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			if (iptr->val.i & 0x20) {
				M_MOV(GET_HIGH_REG(d), GET_LOW_REG(d));
				M_CLR(GET_HIGH_REG(d));
				M_SRLD_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d), GET_LOW_REG(d));
			}
			else {
				M_SRLD_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d), GET_LOW_REG(d));
				M_SRL_IMM(iptr->val.i & 0x3f, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
  			break;

		case ICMD_IAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s2 == d)
				M_AND(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_AND(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_AND_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			if (s2 == GET_LOW_REG(d))
				M_AND(s1, GET_LOW_REG(d));
			else {
				M_INTMOVE(s1, GET_LOW_REG(d));
				M_AND(s2, GET_LOW_REG(d));
			}
			/* REG_ITMP1 probably contains low 32-bit of destination */
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			if (s2 == GET_HIGH_REG(d))
				M_AND(s1, GET_HIGH_REG(d));
			else {
				M_INTMOVE(s1, GET_HIGH_REG(d));
				M_AND(s2, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_AND_IMM(iptr->val.l, GET_LOW_REG(d));
			M_AND_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IOR:        /* ..., val1, val2  ==> ..., val1 | val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s2 == d)
				M_OR(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_OR(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_OR_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LOR:        /* ..., val1, val2  ==> ..., val1 | val2        */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			if (s2 == GET_LOW_REG(d))
				M_OR(s1, GET_LOW_REG(d));
			else {
				M_INTMOVE(s1, GET_LOW_REG(d));
				M_OR(s2, GET_LOW_REG(d));
			}
			/* REG_ITMP1 probably contains low 32-bit of destination */
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			if (s2 == GET_HIGH_REG(d))
				M_OR(s1, GET_HIGH_REG(d));
			else {
				M_INTMOVE(s1, GET_HIGH_REG(d));
				M_OR(s2, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_OR_IMM(iptr->val.l, GET_LOW_REG(d));
			M_OR_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s2 == d)
				M_XOR(s1, d);
			else {
				M_INTMOVE(s1, d);
				M_XOR(s2, d);
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* val.i = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			M_INTMOVE(s1, d);
			M_XOR_IMM(iptr->val.i, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LXOR:       /* ..., val1, val2  ==> ..., val1 ^ val2        */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			if (s2 == GET_LOW_REG(d))
				M_XOR(s1, GET_LOW_REG(d));
			else {
				M_INTMOVE(s1, GET_LOW_REG(d));
				M_XOR(s2, GET_LOW_REG(d));
			}
			/* REG_ITMP1 probably contains low 32-bit of destination */
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			if (s2 == GET_HIGH_REG(d))
				M_XOR(s1, GET_HIGH_REG(d));
			else {
				M_INTMOVE(s1, GET_HIGH_REG(d));
				M_XOR(s2, GET_HIGH_REG(d));
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LXORCONST:  /* ..., value  ==> ..., value ^ constant        */
		                      /* val.l = constant                             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, d);
			M_XOR_IMM(iptr->val.l, GET_LOW_REG(d));
			M_XOR_IMM(iptr->val.l >> 32, GET_HIGH_REG(d));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IINC:       /* ..., value  ==> ..., value + constant        */
		                      /* op1 = variable, val.i = constant             */

			var = &(rd->locals[iptr->op1][TYPE_INT]);
			if (var->flags & INMEMORY) {
				s1 = REG_ITMP1;
				M_ILD(s1, REG_SP, var->regoff * 4);
			}
			else
				s1 = var->regoff;

			/* `inc reg' is slower on p4's (regarding to ia32
			   optimization reference manual and benchmarks) and as
			   fast on athlon's. */

			M_IADD_IMM(iptr->val.i, s1);

			if (var->flags & INMEMORY)
				M_IST(s1, REG_SP, var->regoff * 4);
			break;


		/* floating operations ************************************************/

		case ICMD_FNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			emit_fchs(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DNEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			emit_fchs(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_faddp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DADD:       /* ..., val1, val2  ==> ..., val1 + val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_faddp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fsubp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fsubp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fmulp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fmulp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fdivp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			emit_fdivp(cd);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			/* exchanged to skip fxch */
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
/*  			emit_fxch(cd); */
			emit_fprem(cd);
			emit_wait(cd);
			emit_fnstsw(cd);
			emit_sahf(cd);
			emit_jcc(cd, CC_P, -(2 + 1 + 2 + 1 + 6));
			emit_store(jd, iptr, iptr->dst, d);
			emit_ffree_reg(cd, 0);
			emit_fincstp(cd);
			break;

		case ICMD_DREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			/* exchanged to skip fxch */
			s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);
			s1 = emit_load_s1(jd, iptr, src->prev, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
/*  			emit_fxch(cd); */
			emit_fprem(cd);
			emit_wait(cd);
			emit_fnstsw(cd);
			emit_sahf(cd);
			emit_jcc(cd, CC_P, -(2 + 1 + 2 + 1 + 6));
			emit_store(jd, iptr, iptr->dst, d);
			emit_ffree_reg(cd, 0);
			emit_fincstp(cd);
			break;

		case ICMD_I2F:       /* ..., value  ==> ..., (float) value            */
		case ICMD_I2D:       /* ..., value  ==> ..., (double) value           */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
			if (src->flags & INMEMORY) {
				emit_fildl_membase(cd, REG_SP, src->regoff * 4);

			} else {
				disp = dseg_adds4(cd, 0);
				emit_mov_imm_reg(cd, 0, REG_ITMP1);
				dseg_adddata(cd);
				emit_mov_reg_membase(cd, src->regoff, REG_ITMP1, disp);
				emit_fildl_membase(cd, REG_ITMP1, disp);
			}
  			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_L2F:       /* ..., value  ==> ..., (float) value            */
		case ICMD_L2D:       /* ..., value  ==> ..., (double) value           */

			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
			if (src->flags & INMEMORY) {
				emit_fildll_membase(cd, REG_SP, src->regoff * 4);

			} else {
				log_text("L2F: longs have to be in memory");
				assert(0);
			}
  			emit_store(jd, iptr, iptr->dst, d);
			break;
			
		case ICMD_F2I:       /* ..., value  ==> ..., (int) value              */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);

			emit_mov_imm_reg(cd, 0, REG_ITMP1);
			dseg_adddata(cd);

			/* Round to zero, 53-bit mode, exception masked */
			disp = dseg_adds4(cd, 0x0e7f);
			emit_fldcw_membase(cd, REG_ITMP1, disp);

			if (iptr->dst->flags & INMEMORY) {
				emit_fistpl_membase(cd, REG_SP, iptr->dst->regoff * 4);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

				emit_alu_imm_membase(cd, ALU_CMP, 0x80000000, REG_SP, iptr->dst->regoff * 4);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);

			} else {
				disp = dseg_adds4(cd, 0);
				emit_fistpl_membase(cd, REG_ITMP1, disp);
				emit_mov_membase_reg(cd, REG_ITMP1, disp, iptr->dst->regoff);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

				emit_alu_imm_reg(cd, ALU_CMP, 0x80000000, iptr->dst->regoff);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + ((REG_RESULT == iptr->dst->regoff) ? 0 : 2);
			}

			emit_jcc(cd, CC_NE, disp);

			/* XXX: change this when we use registers */
			emit_flds_membase(cd, REG_SP, src->regoff * 4);
			emit_mov_imm_reg(cd, (ptrint) asm_builtin_f2i, REG_ITMP1);
			emit_call_reg(cd, REG_ITMP1);

			if (iptr->dst->flags & INMEMORY) {
				emit_mov_reg_membase(cd, REG_RESULT, REG_SP, iptr->dst->regoff * 4);

			} else {
				M_INTMOVE(REG_RESULT, iptr->dst->regoff);
			}
			break;

		case ICMD_D2I:       /* ..., value  ==> ..., (int) value              */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);

			emit_mov_imm_reg(cd, 0, REG_ITMP1);
			dseg_adddata(cd);

			/* Round to zero, 53-bit mode, exception masked */
			disp = dseg_adds4(cd, 0x0e7f);
			emit_fldcw_membase(cd, REG_ITMP1, disp);

			if (iptr->dst->flags & INMEMORY) {
				emit_fistpl_membase(cd, REG_SP, iptr->dst->regoff * 4);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

  				emit_alu_imm_membase(cd, ALU_CMP, 0x80000000, REG_SP, iptr->dst->regoff * 4);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);

			} else {
				disp = dseg_adds4(cd, 0);
				emit_fistpl_membase(cd, REG_ITMP1, disp);
				emit_mov_membase_reg(cd, REG_ITMP1, disp, iptr->dst->regoff);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

				emit_alu_imm_reg(cd, ALU_CMP, 0x80000000, iptr->dst->regoff);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + ((REG_RESULT == iptr->dst->regoff) ? 0 : 2);
			}

			emit_jcc(cd, CC_NE, disp);

			/* XXX: change this when we use registers */
			emit_fldl_membase(cd, REG_SP, src->regoff * 4);
			emit_mov_imm_reg(cd, (ptrint) asm_builtin_d2i, REG_ITMP1);
			emit_call_reg(cd, REG_ITMP1);

			if (iptr->dst->flags & INMEMORY) {
				emit_mov_reg_membase(cd, REG_RESULT, REG_SP, iptr->dst->regoff * 4);
			} else {
				M_INTMOVE(REG_RESULT, iptr->dst->regoff);
			}
			break;

		case ICMD_F2L:       /* ..., value  ==> ..., (long) value             */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);

			emit_mov_imm_reg(cd, 0, REG_ITMP1);
			dseg_adddata(cd);

			/* Round to zero, 53-bit mode, exception masked */
			disp = dseg_adds4(cd, 0x0e7f);
			emit_fldcw_membase(cd, REG_ITMP1, disp);

			if (iptr->dst->flags & INMEMORY) {
				emit_fistpll_membase(cd, REG_SP, iptr->dst->regoff * 4);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

  				emit_alu_imm_membase(cd, ALU_CMP, 0x80000000, REG_SP, iptr->dst->regoff * 4 + 4);

				disp = 6 + 4;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2;
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4 + 4);

				emit_jcc(cd, CC_NE, disp);

  				emit_alu_imm_membase(cd, ALU_CMP, 0, REG_SP, iptr->dst->regoff * 4);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);

				emit_jcc(cd, CC_NE, disp);

				/* XXX: change this when we use registers */
				emit_flds_membase(cd, REG_SP, src->regoff * 4);
				emit_mov_imm_reg(cd, (ptrint) asm_builtin_f2l, REG_ITMP1);
				emit_call_reg(cd, REG_ITMP1);
				emit_mov_reg_membase(cd, REG_RESULT, REG_SP, iptr->dst->regoff * 4);
				emit_mov_reg_membase(cd, REG_RESULT2, REG_SP, iptr->dst->regoff * 4 + 4);

			} else {
				log_text("F2L: longs have to be in memory");
				assert(0);
			}
			break;

		case ICMD_D2L:       /* ..., value  ==> ..., (long) value             */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);

			emit_mov_imm_reg(cd, 0, REG_ITMP1);
			dseg_adddata(cd);

			/* Round to zero, 53-bit mode, exception masked */
			disp = dseg_adds4(cd, 0x0e7f);
			emit_fldcw_membase(cd, REG_ITMP1, disp);

			if (iptr->dst->flags & INMEMORY) {
				emit_fistpll_membase(cd, REG_SP, iptr->dst->regoff * 4);

				/* Round to nearest, 53-bit mode, exceptions masked */
				disp = dseg_adds4(cd, 0x027f);
				emit_fldcw_membase(cd, REG_ITMP1, disp);

  				emit_alu_imm_membase(cd, ALU_CMP, 0x80000000, REG_SP, iptr->dst->regoff * 4 + 4);

				disp = 6 + 4;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2;
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);
				disp += 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4 + 4);

				emit_jcc(cd, CC_NE, disp);

  				emit_alu_imm_membase(cd, ALU_CMP, 0, REG_SP, iptr->dst->regoff * 4);

				disp = 3;
				CALCOFFSETBYTES(disp, REG_SP, src->regoff * 4);
				disp += 5 + 2 + 3;
				CALCOFFSETBYTES(disp, REG_SP, iptr->dst->regoff * 4);

				emit_jcc(cd, CC_NE, disp);

				/* XXX: change this when we use registers */
				emit_fldl_membase(cd, REG_SP, src->regoff * 4);
				emit_mov_imm_reg(cd, (ptrint) asm_builtin_d2l, REG_ITMP1);
				emit_call_reg(cd, REG_ITMP1);
				emit_mov_reg_membase(cd, REG_RESULT, REG_SP, iptr->dst->regoff * 4);
				emit_mov_reg_membase(cd, REG_RESULT2, REG_SP, iptr->dst->regoff * 4 + 4);

			} else {
				log_text("D2L: longs have to be in memory");
				assert(0);
			}
			break;

		case ICMD_F2D:       /* ..., value  ==> ..., (double) value           */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			/* nothing to do */
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_D2F:       /* ..., value  ==> ..., (float) value            */

			s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			/* nothing to do */
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FCMPL:      /* ..., val1, val2  ==> ..., val1 fcmpl val2    */
		case ICMD_DCMPL:

			/* exchanged to skip fxch */
			s2 = emit_load_s2(jd, iptr, src->prev, REG_FTMP1);
			s1 = emit_load_s1(jd, iptr, src, REG_FTMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
/*    			emit_fxch(cd); */
			emit_fucompp(cd);
			emit_fnstsw(cd);
			emit_test_imm_reg(cd, 0x400, EAX);    /* unordered treat as GT */
			emit_jcc(cd, CC_E, 6);
			emit_alu_imm_reg(cd, ALU_AND, 0x000000ff, EAX);
 			emit_sahf(cd);
			emit_mov_imm_reg(cd, 0, d);    /* does not affect flags */
  			emit_jcc(cd, CC_E, 6 + 3 + 5 + 3);
  			emit_jcc(cd, CC_B, 3 + 5);
			emit_alu_imm_reg(cd, ALU_SUB, 1, d);
			emit_jmp_imm(cd, 3);
			emit_alu_imm_reg(cd, ALU_ADD, 1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_FCMPG:      /* ..., val1, val2  ==> ..., val1 fcmpg val2    */
		case ICMD_DCMPG:

			/* exchanged to skip fxch */
			s2 = emit_load_s2(jd, iptr, src->prev, REG_FTMP1);
			s1 = emit_load_s1(jd, iptr, src, REG_FTMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
/*    			emit_fxch(cd); */
			emit_fucompp(cd);
			emit_fnstsw(cd);
			emit_test_imm_reg(cd, 0x400, EAX);    /* unordered treat as LT */
			emit_jcc(cd, CC_E, 3);
			emit_movb_imm_reg(cd, 1, REG_AH);
 			emit_sahf(cd);
			emit_mov_imm_reg(cd, 0, d);    /* does not affect flags */
  			emit_jcc(cd, CC_E, 6 + 3 + 5 + 3);
  			emit_jcc(cd, CC_B, 3 + 5);
			emit_alu_imm_reg(cd, ALU_SUB, 1, d);
			emit_jmp_imm(cd, 3);
			emit_alu_imm_reg(cd, ALU_ADD, 1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;


		/* memory operations **************************************************/

		case ICMD_ARRAYLENGTH: /* ..., arrayref  ==> ..., length              */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			gen_nullptr_check(s1);
			M_ILD(d, s1, OFFSET(java_arrayheader, size));
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_BALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
   			emit_movsbl_memindex_reg(cd, OFFSET(java_bytearray, data[0]), s1, s2, 0, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_CALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movzwl_memindex_reg(cd, OFFSET(java_chararray, data[0]), s1, s2, 1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;			

		case ICMD_SALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movswl_memindex_reg(cd, OFFSET(java_shortarray, data[0]), s1, s2, 1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_IALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_memindex_reg(cd, OFFSET(java_intarray, data[0]), s1, s2, 2, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_LALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			assert(iptr->dst->flags & INMEMORY);
			emit_mov_memindex_reg(cd, OFFSET(java_longarray, data[0]), s1, s2, 3, REG_ITMP3);
			emit_mov_reg_membase(cd, REG_ITMP3, REG_SP, iptr->dst->regoff * 4);
			emit_mov_memindex_reg(cd, OFFSET(java_longarray, data[0]) + 4, s1, s2, 3, REG_ITMP3);
			emit_mov_reg_membase(cd, REG_ITMP3, REG_SP, iptr->dst->regoff * 4 + 4);
			break;

		case ICMD_FALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_flds_memindex(cd, OFFSET(java_floatarray, data[0]), s1, s2, 2);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_DALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP3);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_fldl_memindex(cd, OFFSET(java_doublearray, data[0]), s1, s2, 3);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_AALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP1);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_memindex_reg(cd, OFFSET(java_objectarray, data[0]), s1, s2, 2, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;


		case ICMD_BASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);
			if (s3 >= EBP) { /* because EBP, ESI, EDI have no xH and xL nibbles */
				M_INTMOVE(s3, REG_ITMP3);
				s3 = REG_ITMP3;
			}
			emit_movb_reg_memindex(cd, s3, OFFSET(java_bytearray, data[0]), s1, s2, 0);
			break;

		case ICMD_CASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);
			emit_movw_reg_memindex(cd, s3, OFFSET(java_chararray, data[0]), s1, s2, 1);
			break;

		case ICMD_SASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);
			emit_movw_reg_memindex(cd, s3, OFFSET(java_shortarray, data[0]), s1, s2, 1);
			break;

		case ICMD_IASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);
			emit_mov_reg_memindex(cd, s3, OFFSET(java_intarray, data[0]), s1, s2, 2);
			break;

		case ICMD_LASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			assert(src->flags & INMEMORY);
			emit_mov_membase_reg(cd, REG_SP, src->regoff * 4, REG_ITMP3);
			emit_mov_reg_memindex(cd, REG_ITMP3, OFFSET(java_longarray, data[0]), s1, s2, 3);
			emit_mov_membase_reg(cd, REG_SP, src->regoff * 4 + 4, REG_ITMP3);
			emit_mov_reg_memindex(cd, REG_ITMP3, OFFSET(java_longarray, data[0]) + 4, s1, s2, 3);
			break;

		case ICMD_FASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_FTMP1);
			emit_fstps_memindex(cd, OFFSET(java_floatarray, data[0]), s1, s2, 2);
			break;

		case ICMD_DASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_FTMP1);
			emit_fstpl_memindex(cd, OFFSET(java_doublearray, data[0]), s1, s2, 3);
			break;

		case ICMD_AASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);

			M_AST(s1, REG_SP, 0 * 4);
			M_AST(s3, REG_SP, 1 * 4);
			M_MOV_IMM(BUILTIN_canstore, REG_ITMP1);
			M_CALL(REG_ITMP1);
			M_TEST(REG_RESULT);
			M_BEQ(0);
			codegen_add_arraystoreexception_ref(cd);

			s1 = emit_load_s1(jd, iptr, src->prev->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src->prev, REG_ITMP2);
			s3 = emit_load_s3(jd, iptr, src, REG_ITMP3);
			emit_mov_reg_memindex(cd, s3, OFFSET(java_objectarray, data[0]), s1, s2, 2);
			break;

		case ICMD_BASTORECONST: /* ..., arrayref, index  ==> ...              */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movb_imm_memindex(cd, iptr->val.i, OFFSET(java_bytearray, data[0]), s1, s2, 0);
			break;

		case ICMD_CASTORECONST:   /* ..., arrayref, index  ==> ...            */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movw_imm_memindex(cd, iptr->val.i, OFFSET(java_chararray, data[0]), s1, s2, 1);
			break;

		case ICMD_SASTORECONST:   /* ..., arrayref, index  ==> ...            */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_movw_imm_memindex(cd, iptr->val.i, OFFSET(java_shortarray, data[0]), s1, s2, 1);
			break;

		case ICMD_IASTORECONST: /* ..., arrayref, index  ==> ...              */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_imm_memindex(cd, iptr->val.i, OFFSET(java_intarray, data[0]), s1, s2, 2);
			break;

		case ICMD_LASTORECONST: /* ..., arrayref, index  ==> ...              */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_imm_memindex(cd, (u4) (iptr->val.l & 0x00000000ffffffff), OFFSET(java_longarray, data[0]), s1, s2, 3);
			emit_mov_imm_memindex(cd, (u4) (iptr->val.l >> 32), OFFSET(java_longarray, data[0]) + 4, s1, s2, 3);
			break;

		case ICMD_AASTORECONST: /* ..., arrayref, index  ==> ...              */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			if (iptr->op1 == 0) {
				gen_nullptr_check(s1);
				gen_bound_check;
			}
			emit_mov_imm_memindex(cd, 0, OFFSET(java_objectarray, data[0]), s1, s2, 2);
			break;


		case ICMD_GETSTATIC:  /* ...  ==> ..., value                          */
		                      /* op1 = type, val.a = field address            */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_get_putstatic,
									INSTRUCTION_UNRESOLVED_FIELD(iptr), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else {
				fieldinfo *fi = INSTRUCTION_RESOLVED_FIELDINFO(iptr);

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					codegen_addpatchref(cd, PATCHER_clinit, fi->class, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				disp = (ptrint) &(fi->value);
  			}

			M_MOV_IMM(disp, REG_ITMP1);
			switch (iptr->op1) {
			case TYPE_INT:
			case TYPE_ADR:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
				M_ILD(d, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP23_PACKED);
				M_LLD(d, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
				M_FLD(d, REG_ITMP1, 0);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
				M_DLD(d, REG_ITMP1, 0);
				break;
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_PUTSTATIC:  /* ..., value  ==> ...                          */
		                      /* op1 = type, val.a = field address            */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_get_putstatic,
									INSTRUCTION_UNRESOLVED_FIELD(iptr), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else {
				fieldinfo *fi = INSTRUCTION_RESOLVED_FIELDINFO(iptr);

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					codegen_addpatchref(cd, PATCHER_clinit, fi->class, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				disp = (ptrint) &(fi->value);
  			}

			M_MOV_IMM(disp, REG_ITMP1);
			switch (iptr->op1) {
			case TYPE_INT:
			case TYPE_ADR:
				s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
				M_IST(s2, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
				s2 = emit_load_s2(jd, iptr, src, REG_ITMP23_PACKED);
				M_LST(s2, REG_ITMP1, 0);
				break;
			case TYPE_FLT:
				s2 = emit_load_s2(jd, iptr, src, REG_FTMP1);
				emit_fstps_membase(cd, REG_ITMP1, 0);
				break;
			case TYPE_DBL:
				s2 = emit_load_s2(jd, iptr, src, REG_FTMP1);
				emit_fstpl_membase(cd, REG_ITMP1, 0);
				break;
			}
			break;

		case ICMD_PUTSTATICCONST: /* ...  ==> ...                             */
		                          /* val = value (in current instruction)     */
		                          /* op1 = type, val.a = field address (in    */
		                          /* following NOP)                           */

			if (INSTRUCTION_IS_UNRESOLVED(iptr + 1)) {
				codegen_addpatchref(cd, PATCHER_get_putstatic,
									INSTRUCTION_UNRESOLVED_FIELD(iptr + 1), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else {
				fieldinfo *fi = INSTRUCTION_RESOLVED_FIELDINFO(iptr + 1);

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->class)) {
					codegen_addpatchref(cd, PATCHER_clinit, fi->class, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				disp = (ptrint) &(fi->value);
  			}

			M_MOV_IMM(disp, REG_ITMP1);
			switch (iptr[1].op1) {
			case TYPE_INT:
			case TYPE_FLT:
			case TYPE_ADR:
				M_IST_IMM(iptr->val.i, REG_ITMP1, 0);
				break;
			case TYPE_LNG:
			case TYPE_DBL:
				M_LST_IMM(iptr->val.l, REG_ITMP1, 0);
				break;
			}
			break;

		case ICMD_GETFIELD:   /* .., objectref.  ==> ..., value               */
		                      /* op1 = type, val.i = field offset             */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			gen_nullptr_check(s1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_getfield,
									INSTRUCTION_UNRESOLVED_FIELD(iptr), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else
				disp = INSTRUCTION_RESOLVED_FIELDINFO(iptr)->offset;

			switch (iptr->op1) {
			case TYPE_INT:
			case TYPE_ADR:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
				M_ILD32(d, s1, disp);
				break;
			case TYPE_LNG:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP23_PACKED);
				M_LLD32(d, s1, disp);
				break;
			case TYPE_FLT:
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
				M_FLD32(d, s1, disp);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_FTMP1);
				M_DLD32(d, s1, disp);
				break;
			}
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_PUTFIELD:   /* ..., objectref, value  ==> ...               */
		                      /* op1 = type, val.a = field address            */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			gen_nullptr_check(s1);

			/* must be done here because of code patching */

			if (!IS_FLT_DBL_TYPE(iptr->op1)) {
				if (IS_2_WORD_TYPE(iptr->op1))
					s2 = emit_load_s2(jd, iptr, src, REG_ITMP23_PACKED);
				else
					s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			}
			else
				s2 = emit_load_s2(jd, iptr, src, REG_FTMP2);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				codegen_addpatchref(cd, PATCHER_putfield,
									INSTRUCTION_UNRESOLVED_FIELD(iptr), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else
				disp = INSTRUCTION_RESOLVED_FIELDINFO(iptr)->offset;

			switch (iptr->op1) {
			case TYPE_INT:
			case TYPE_ADR:
				M_IST32(s2, s1, disp);
				break;
			case TYPE_LNG:
				M_LST32(s2, s1, disp);
				break;
			case TYPE_FLT:
				emit_fstps_membase32(cd, s1, disp);
				break;
			case TYPE_DBL:
				emit_fstpl_membase32(cd, s1, disp);
				break;
			}
			break;

		case ICMD_PUTFIELDCONST:  /* ..., objectref  ==> ...                  */
		                          /* val = value (in current instruction)     */
		                          /* op1 = type, val.a = field address (in    */
		                          /* following NOP)                           */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			gen_nullptr_check(s1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr + 1)) {
				codegen_addpatchref(cd, PATCHER_putfieldconst,
									INSTRUCTION_UNRESOLVED_FIELD(iptr + 1), 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else
				disp = INSTRUCTION_RESOLVED_FIELDINFO(iptr + 1)->offset;

			switch (iptr[1].op1) {
			case TYPE_INT:
			case TYPE_FLT:
			case TYPE_ADR:
				M_IST32_IMM(iptr->val.i, s1, disp);
				break;
			case TYPE_LNG:
			case TYPE_DBL:
				M_LST32_IMM(iptr->val.l, s1, disp);
				break;
			}
			break;


		/* branch operations **************************************************/

		case ICMD_ATHROW:       /* ..., objectref ==> ... (, objectref)       */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1_XPTR);

#ifdef ENABLE_VERIFIER
			if (iptr->val.a) {
				codegen_addpatchref(cd, PATCHER_athrow_areturn,
									(unresolved_class *) iptr->val.a, 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}
			}
#endif /* ENABLE_VERIFIER */

			M_CALL_IMM(0);                            /* passing exception pc */
			M_POP(REG_ITMP2_XPC);

			M_MOV_IMM(asm_handle_exception, REG_ITMP3);
			M_JMP(REG_ITMP3);
			break;

		case ICMD_INLINE_GOTO:

			M_COPY(src, iptr->dst);
			/* FALLTHROUGH! */

		case ICMD_GOTO:         /* ... ==> ...                                */
		                        /* op1 = target JavaVM pc                     */

			M_JMP_IMM(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			ALIGNCODENOP;
			break;

		case ICMD_JSR:          /* ... ==> ...                                */
		                        /* op1 = target JavaVM pc                     */

  			M_CALL_IMM(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;
			
		case ICMD_RET:          /* ... ==> ...                                */
		                        /* op1 = local variable                       */

			var = &(rd->locals[iptr->op1][TYPE_ADR]);
			if (var->flags & INMEMORY) {
				M_ALD(REG_ITMP1, REG_SP, var->regoff * 4);
				M_JMP(REG_ITMP1);
			}
			else
				M_JMP(var->regoff);
			break;

		case ICMD_IFNULL:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_TEST(s1);
			M_BEQ(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFNONNULL:    /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_TEST(s1);
			M_BNE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFEQ:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BEQ(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFLT:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BLT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFLE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BLE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFNE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BNE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFGT:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BGT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IFGE:         /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.i = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			M_CMP_IMM(iptr->val.i, s1);
			M_BGE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LEQ:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			if (iptr->val.l == 0) {
				M_INTMOVE(GET_LOW_REG(s1), REG_ITMP1);
				M_OR(GET_HIGH_REG(s1), REG_ITMP1);
			}
			else {
				M_LNGMOVE(s1, REG_ITMP12_PACKED);
				M_XOR_IMM(iptr->val.l, REG_ITMP1);
				M_XOR_IMM(iptr->val.l >> 32, REG_ITMP2);
				M_OR(REG_ITMP2, REG_ITMP1);
			}
			M_BEQ(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LLT:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			if (iptr->val.l == 0) {
				/* If high 32-bit are less than zero, then the 64-bits
				   are too. */
				s1 = emit_load_s1_high(jd, iptr, src, REG_ITMP2);
				M_CMP_IMM(0, s1);
				M_BLT(0);
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
				M_CMP_IMM(iptr->val.l >> 32, GET_HIGH_REG(s1));
				M_BLT(0);
				codegen_addreference(cd, (basicblock *) iptr->target);
				M_BGT(6 + 6);
				M_CMP_IMM32(iptr->val.l, GET_LOW_REG(s1));
				M_BB(0);
			}			
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LLE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			M_CMP_IMM(iptr->val.l >> 32, GET_HIGH_REG(s1));
			M_BLT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			M_BGT(6 + 6);
			M_CMP_IMM32(iptr->val.l, GET_LOW_REG(s1));
			M_BBE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LNE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			if (iptr->val.l == 0) {
				M_INTMOVE(GET_LOW_REG(s1), REG_ITMP1);
				M_OR(GET_HIGH_REG(s1), REG_ITMP1);
			}
			else {
				M_LNGMOVE(s1, REG_ITMP12_PACKED);
				M_XOR_IMM(iptr->val.l, REG_ITMP1);
				M_XOR_IMM(iptr->val.l >> 32, REG_ITMP2);
				M_OR(REG_ITMP2, REG_ITMP1);
			}
			M_BNE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LGT:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
			M_CMP_IMM(iptr->val.l >> 32, GET_HIGH_REG(s1));
			M_BGT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			M_BLT(6 + 6);
			M_CMP_IMM32(iptr->val.l, GET_LOW_REG(s1));
			M_BA(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LGE:       /* ..., value ==> ...                         */
		                        /* op1 = target JavaVM pc, val.l = constant   */

			if (iptr->val.l == 0) {
				/* If high 32-bit are greater equal zero, then the
				   64-bits are too. */
				s1 = emit_load_s1_high(jd, iptr, src, REG_ITMP2);
				M_CMP_IMM(0, s1);
				M_BGE(0);
			}
			else {
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
				M_CMP_IMM(iptr->val.l >> 32, GET_HIGH_REG(s1));
				M_BGT(0);
				codegen_addreference(cd, (basicblock *) iptr->target);
				M_BLT(6 + 6);
				M_CMP_IMM32(iptr->val.l, GET_LOW_REG(s1));
				M_BAE(0);
			}
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPEQ:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPEQ:    /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BEQ(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPEQ:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_INTMOVE(s1, REG_ITMP1);
			M_XOR(s2, REG_ITMP1);
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP2);
			M_XOR(s2, REG_ITMP2);
			M_OR(REG_ITMP1, REG_ITMP2);
			M_BEQ(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPNE:    /* ..., value, value ==> ...                  */
		case ICMD_IF_ACMPNE:    /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BNE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPNE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_INTMOVE(s1, REG_ITMP1);
			M_XOR(s2, REG_ITMP1);
			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP2);
			M_XOR(s2, REG_ITMP2);
			M_OR(REG_ITMP1, REG_ITMP2);
			M_BNE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPLT:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BLT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPLT:    /* ..., value, value ==> ...                  */
	                            /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BLT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_BGT(2 + 6);
			M_CMP(s2, s1);
			M_BB(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPGT:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BGT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPGT:    /* ..., value, value ==> ...                  */
                                /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BGT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_BLT(2 + 6);
			M_CMP(s2, s1);
			M_BA(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPLE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BLE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPLE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BLT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_BGT(2 + 6);
			M_CMP(s2, s1);
			M_BBE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_ICMPGE:    /* ..., value, value ==> ...                  */
		                        /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BGE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;

		case ICMD_IF_LCMPGE:    /* ..., value, value ==> ...                  */
	                            /* op1 = target JavaVM pc                     */

			s1 = emit_load_s1_high(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_high(jd, iptr, src, REG_ITMP2);
			M_CMP(s2, s1);
			M_BGT(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			s1 = emit_load_s1_low(jd, iptr, src->prev, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, src, REG_ITMP2);
			M_BLT(2 + 6);
			M_CMP(s2, s1);
			M_BAE(0);
			codegen_addreference(cd, (basicblock *) iptr->target);
			break;


		case ICMD_IRETURN:      /* ..., retvalue ==> ...                      */

			s1 = emit_load_s1(jd, iptr, src, REG_RESULT);
			M_INTMOVE(s1, REG_RESULT);
			goto nowperformreturn;

		case ICMD_LRETURN:      /* ..., retvalue ==> ...                      */

			s1 = emit_load_s1(jd, iptr, src, REG_RESULT_PACKED);
			M_LNGMOVE(s1, REG_RESULT_PACKED);
			goto nowperformreturn;

		case ICMD_ARETURN:      /* ..., retvalue ==> ...                      */

			s1 = emit_load_s1(jd, iptr, src, REG_RESULT);
			M_INTMOVE(s1, REG_RESULT);

#ifdef ENABLE_VERIFIER
			if (iptr->val.a) {
				codegen_addpatchref(cd, PATCHER_athrow_areturn,
									(unresolved_class *) iptr->val.a, 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}
			}
#endif /* ENABLE_VERIFIER */
			goto nowperformreturn;

		case ICMD_FRETURN:      /* ..., retvalue ==> ...                      */
		case ICMD_DRETURN:

			s1 = emit_load_s1(jd, iptr, src, REG_FRESULT);
			goto nowperformreturn;

		case ICMD_RETURN:      /* ...  ==> ...                                */

nowperformreturn:
			{
			s4 i, p;
			
  			p = stackframesize;
			
#if !defined(NDEBUG)
			/* call trace function */

			if (opt_verbosecall) {
				M_ISUB_IMM(4 + 8 + 8 + 4, REG_SP);

				emit_mov_imm_membase(cd, (s4) m, REG_SP, 0);

				emit_mov_reg_membase(cd, REG_RESULT, REG_SP, 4);
				emit_mov_reg_membase(cd, REG_RESULT2, REG_SP, 4 + 4);
				
				emit_fstl_membase(cd, REG_SP, 4 + 8);
				emit_fsts_membase(cd, REG_SP, 4 + 8 + 8);

  				emit_mov_imm_reg(cd, (s4) builtin_displaymethodstop, REG_ITMP1);
				emit_call_reg(cd, REG_ITMP1);

				emit_mov_membase_reg(cd, REG_SP, 4, REG_RESULT);
				emit_mov_membase_reg(cd, REG_SP, 4 + 4, REG_RESULT2);

				emit_alu_imm_reg(cd, ALU_ADD, 4 + 8 + 8 + 4, REG_SP);
			}
#endif /* !defined(NDEBUG) */

#if defined(ENABLE_THREADS)
			if (checksync && (m->flags & ACC_SYNCHRONIZED)) {
				M_ALD(REG_ITMP2, REG_SP, rd->memuse * 4);

				/* we need to save the proper return value */
				switch (iptr->opc) {
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					M_IST(REG_RESULT, REG_SP, rd->memuse * 4);
					break;

				case ICMD_LRETURN:
					M_LST(REG_RESULT_PACKED, REG_SP, rd->memuse * 4);
					break;

				case ICMD_FRETURN:
					emit_fstps_membase(cd, REG_SP, rd->memuse * 4);
					break;

				case ICMD_DRETURN:
					emit_fstpl_membase(cd, REG_SP, rd->memuse * 4);
					break;
				}

				M_AST(REG_ITMP2, REG_SP, 0);
				M_MOV_IMM(LOCK_monitor_exit, REG_ITMP3);
				M_CALL(REG_ITMP3);

				/* and now restore the proper return value */
				switch (iptr->opc) {
				case ICMD_IRETURN:
				case ICMD_ARETURN:
					M_ILD(REG_RESULT, REG_SP, rd->memuse * 4);
					break;

				case ICMD_LRETURN:
					M_LLD(REG_RESULT_PACKED, REG_SP, rd->memuse * 4);
					break;

				case ICMD_FRETURN:
					emit_flds_membase(cd, REG_SP, rd->memuse * 4);
					break;

				case ICMD_DRETURN:
					emit_fldl_membase(cd, REG_SP, rd->memuse * 4);
					break;
				}
			}
#endif

			/* restore saved registers */

			for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
				p--; M_ALD(rd->savintregs[i], REG_SP, p * 4);
			}

			for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
  				p--;
				emit_fldl_membase(cd, REG_SP, p * 4);
				if (iptr->opc == ICMD_FRETURN || iptr->opc == ICMD_DRETURN) {
					assert(0);
/* 					emit_fstp_reg(cd, rd->savfltregs[i] + fpu_st_offset + 1); */
				} else {
					assert(0);
/* 					emit_fstp_reg(cd, rd->savfltregs[i] + fpu_st_offset); */
				}
			}

			/* deallocate stack */

			if (stackframesize)
				M_AADD_IMM(stackframesize * 4, REG_SP);

			emit_ret(cd);
			}
			break;


		case ICMD_TABLESWITCH:  /* ..., index ==> ...                         */
			{
				s4 i, l, *s4ptr;
				void **tptr;

				tptr = (void **) iptr->target;

				s4ptr = iptr->val.a;
				l = s4ptr[1];                          /* low     */
				i = s4ptr[2];                          /* high    */

				s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
				M_INTMOVE(s1, REG_ITMP1);

				if (l != 0)
					M_ISUB_IMM(l, REG_ITMP1);

				i = i - l + 1;

                /* range check */

				M_CMP_IMM(i - 1, REG_ITMP1);
				M_BA(0);
				codegen_addreference(cd, (basicblock *) tptr[0]);

				/* build jump table top down and use address of lowest entry */

				tptr += i;

				while (--i >= 0) {
					dseg_addtarget(cd, (basicblock *) tptr[0]); 
					--tptr;
				}

				/* length of dataseg after last dseg_addtarget is used
				   by load */

				M_MOV_IMM(0, REG_ITMP2);
				dseg_adddata(cd);
				emit_mov_memindex_reg(cd, -(cd->dseglen), REG_ITMP2, REG_ITMP1, 2, REG_ITMP1);
				M_JMP(REG_ITMP1);
			}
			break;


		case ICMD_LOOKUPSWITCH: /* ..., key ==> ...                           */
 			{
				s4 i, l, val, *s4ptr;
				void **tptr;

				tptr = (void **) iptr->target;

				s4ptr = iptr->val.a;
				l = s4ptr[0];                          /* default  */
				i = s4ptr[1];                          /* count    */
			
				MCODECHECK((i<<2)+8);
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);

				while (--i >= 0) {
					s4ptr += 2;
					++tptr;

					val = s4ptr[0];
					M_CMP_IMM(val, s1);
					M_BEQ(0);
					codegen_addreference(cd, (basicblock *) tptr[0]); 
				}

				M_JMP_IMM(0);
			
				tptr = (void **) iptr->target;
				codegen_addreference(cd, (basicblock *) tptr[0]);
			}
			break;

		case ICMD_BUILTIN:      /* ..., [arg1, [arg2 ...]] ==> ...            */
		                        /* op1 = arg count val.a = builtintable entry */

			bte = iptr->val.a;
			md = bte->md;
			goto gen_method;

		case ICMD_INVOKESTATIC: /* ..., [arg1, [arg2 ...]] ==> ...            */
		                        /* op1 = arg count, val.a = method pointer    */

		case ICMD_INVOKESPECIAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
		case ICMD_INVOKEVIRTUAL:/* op1 = arg count, val.a = method pointer    */
		case ICMD_INVOKEINTERFACE:

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				md = INSTRUCTION_UNRESOLVED_METHOD(iptr)->methodref->parseddesc.md;
				lm = NULL;
			}
			else {
				lm = INSTRUCTION_RESOLVED_METHODINFO(iptr);
				md = lm->parseddesc;
			}

gen_method:
			s3 = md->paramcount;

			MCODECHECK((s3 << 1) + 64);

			/* copy arguments to registers or stack location                  */

			for (s3 = s3 - 1; s3 >= 0; s3--, src = src->prev) {
				if (src->varkind == ARGVAR)
					continue;
				if (IS_INT_LNG_TYPE(src->type)) {
					if (!md->params[s3].inmemory) {
						log_text("No integer argument registers available!");
						assert(0);

					} else {
						if (IS_2_WORD_TYPE(src->type)) {
							d = emit_load_s1(jd, iptr, src, REG_ITMP12_PACKED);
							M_LST(d, REG_SP, md->params[s3].regoff * 4);
						} else {
							d = emit_load_s1(jd, iptr, src, REG_ITMP1);
							M_IST(d, REG_SP, md->params[s3].regoff * 4);
						}
					}

				} else {
					if (!md->params[s3].inmemory) {
						s1 = rd->argfltregs[md->params[s3].regoff];
						d = emit_load_s1(jd, iptr, src, s1);
						M_FLTMOVE(d, s1);

					} else {
						d = emit_load_s1(jd, iptr, src, REG_FTMP1);
						if (IS_2_WORD_TYPE(src->type))
							M_DST(d, REG_SP, md->params[s3].regoff * 4);
						else
							M_FST(d, REG_SP, md->params[s3].regoff * 4);
					}
				}
			} /* end of for */

			switch (iptr->opc) {
			case ICMD_BUILTIN:
				disp = (ptrint) bte->fp;
				d = md->returntype.type;

				M_MOV_IMM(disp, REG_ITMP1);
				M_CALL(REG_ITMP1);

				/* if op1 == true, we need to check for an exception */

				if (iptr->op1 == true) {
					M_TEST(REG_RESULT);
					M_BEQ(0);
					codegen_add_fillinstacktrace_ref(cd);
				}
				break;

			case ICMD_INVOKESPECIAL:
				M_ALD(REG_ITMP1, REG_SP, 0);
				M_TEST(REG_ITMP1);
				M_BEQ(0);
				codegen_add_nullpointerexception_ref(cd);

				/* fall through */

			case ICMD_INVOKESTATIC:
				if (lm == NULL) {
					unresolved_method *um = INSTRUCTION_UNRESOLVED_METHOD(iptr);

					codegen_addpatchref(cd, PATCHER_invokestatic_special,
										um, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}

					disp = 0;
					d = md->returntype.type;
				}
				else {
					disp = (ptrint) lm->stubroutine;
					d = lm->parseddesc->returntype.type;
				}

				M_MOV_IMM(disp, REG_ITMP2);
				M_CALL(REG_ITMP2);
				break;

			case ICMD_INVOKEVIRTUAL:
				M_ALD(REG_ITMP1, REG_SP, 0 * 4);
				gen_nullptr_check(REG_ITMP1);

				if (lm == NULL) {
					unresolved_method *um = INSTRUCTION_UNRESOLVED_METHOD(iptr);

					codegen_addpatchref(cd, PATCHER_invokevirtual, um, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}

					s1 = 0;
					d = md->returntype.type;
				}
				else {
					s1 = OFFSET(vftbl_t, table[0]) +
						sizeof(methodptr) * lm->vftblindex;
					d = md->returntype.type;
				}

				M_ALD(REG_METHODPTR, REG_ITMP1,
					  OFFSET(java_objectheader, vftbl));
				M_ALD32(REG_ITMP3, REG_METHODPTR, s1);
				M_CALL(REG_ITMP3);
				break;

			case ICMD_INVOKEINTERFACE:
				M_ALD(REG_ITMP1, REG_SP, 0 * 4);
				gen_nullptr_check(REG_ITMP1);

				if (lm == NULL) {
					unresolved_method *um = INSTRUCTION_UNRESOLVED_METHOD(iptr);

					codegen_addpatchref(cd, PATCHER_invokeinterface, um, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}

					s1 = 0;
					s2 = 0;
					d = md->returntype.type;
				}
				else {
					s1 = OFFSET(vftbl_t, interfacetable[0]) -
						sizeof(methodptr) * lm->class->index;

					s2 = sizeof(methodptr) * (lm - lm->class->methods);

					d = md->returntype.type;
				}

				M_ALD(REG_METHODPTR, REG_ITMP1,
					  OFFSET(java_objectheader, vftbl));
				M_ALD32(REG_METHODPTR, REG_METHODPTR, s1);
				M_ALD32(REG_ITMP3, REG_METHODPTR, s2);
				M_CALL(REG_ITMP3);
				break;
			}

			/* d contains return type */

			if (d != TYPE_VOID) {
				if (IS_INT_LNG_TYPE(iptr->dst->type)) {
					if (IS_2_WORD_TYPE(iptr->dst->type)) {
						s1 = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT_PACKED);
						M_LNGMOVE(REG_RESULT_PACKED, s1);
					}
					else {
						s1 = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT);
						M_INTMOVE(REG_RESULT, s1);
					}
				}
				else {
					s1 = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_NULL);
				}
				emit_store(jd, iptr, iptr->dst, s1);
			}
			break;


		case ICMD_CHECKCAST:  /* ..., objectref ==> ..., objectref            */
		                      /* op1:   0 == array, 1 == class                */
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

			if (iptr->op1 == 1) {
				/* object type cast-check */

				classinfo *super;
				vftbl_t   *supervftbl;
				s4         superindex;

				super = (classinfo *) iptr->val.a;

				if (!super) {
					superindex = 0;
					supervftbl = NULL;

				} else {
					superindex = super->index;
					supervftbl = super->vftbl;
				}
			
#if defined(ENABLE_THREADS)
				codegen_threadcritrestart(cd, cd->mcodeptr - cd->mcodebase);
#endif
				s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);

				/* calculate interface checkcast code size */

				s2 = 2; /* mov_membase_reg */
				CALCOFFSETBYTES(s2, s1, OFFSET(java_objectheader, vftbl));

				s2 += (2 + 4 /* mov_membase32_reg */ + 2 + 4 /* sub imm32 */ +
					   2 /* test */ + 6 /* jcc */ + 2 + 4 /* mov_membase32_reg */ +
					   2 /* test */ + 6 /* jcc */);

				if (!super)
					s2 += (opt_showdisassemble ? 5 : 0);

				/* calculate class checkcast code size */

				s3 = 2; /* mov_membase_reg */
				CALCOFFSETBYTES(s3, s1, OFFSET(java_objectheader, vftbl));

				s3 += 5 /* mov_imm_reg */ + 2 + 4 /* mov_membase32_reg */;

#if 0
				if (s1 != REG_ITMP1) {
					a += 2;
					CALCOFFSETBYTES(a, REG_ITMP3, OFFSET(vftbl_t, baseval));
				
					a += 2;
					CALCOFFSETBYTES(a, REG_ITMP3, OFFSET(vftbl_t, diffval));
				
					a += 2;
				
				} else
#endif
					{
						s3 += (2 + 4 /* mov_membase32_reg */ + 2 /* sub */ +
							   5 /* mov_imm_reg */ + 2 /* mov_membase_reg */);
						CALCOFFSETBYTES(s3, REG_ITMP3, OFFSET(vftbl_t, diffval));
					}

				s3 += 2 /* cmp */ + 6 /* jcc */;

				if (super == NULL)
					s3 += (opt_showdisassemble ? 5 : 0);

				/* if class is not resolved, check which code to call */

				if (super == NULL) {
					M_TEST(s1);
					M_BEQ(5 + (opt_showdisassemble ? 5 : 0) + 6 + 6 + s2 + 5 + s3);

					codegen_addpatchref(cd, PATCHER_checkcast_instanceof_flags,
										(constant_classref *) iptr->target, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}

					M_MOV_IMM(0, REG_ITMP2);                  /* super->flags */
					M_AND_IMM32(ACC_INTERFACE, REG_ITMP2);
					M_BEQ(s2 + 5);
				}

				/* interface checkcast code */

				if ((super == NULL) || (super->flags & ACC_INTERFACE)) {
					if (super != NULL) {
						M_TEST(s1);
						M_BEQ(s2);
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));

					if (super == NULL) {
						codegen_addpatchref(cd,
											PATCHER_checkcast_instanceof_interface,
											(constant_classref *) iptr->target,
											0);

						if (opt_showdisassemble) {
							M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
						}
					}

					M_ILD32(REG_ITMP3,
							REG_ITMP2, OFFSET(vftbl_t, interfacetablelength));
					M_ISUB_IMM32(superindex, REG_ITMP3);
					M_TEST(REG_ITMP3);
					M_BLE(0);
					codegen_add_classcastexception_ref(cd, s1);
					M_ALD32(REG_ITMP3, REG_ITMP2,
							OFFSET(vftbl_t, interfacetable[0]) -
							superindex * sizeof(methodptr*));
					M_TEST(REG_ITMP3);
					M_BEQ(0);
					codegen_add_classcastexception_ref(cd, s1);

					if (super == NULL)
						M_JMP_IMM(s3);
				}

				/* class checkcast code */

				if ((super == NULL) || !(super->flags & ACC_INTERFACE)) {
					if (super != NULL) {
						M_TEST(s1);
						M_BEQ(s3);
					}

					M_ALD(REG_ITMP2, s1, OFFSET(java_objectheader, vftbl));

					if (super == NULL) {
						codegen_addpatchref(cd, PATCHER_checkcast_class,
											(constant_classref *) iptr->target,
											0);

						if (opt_showdisassemble) {
							M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
						}
					}

					M_MOV_IMM(supervftbl, REG_ITMP3);
#if defined(ENABLE_THREADS)
					codegen_threadcritstart(cd, cd->mcodeptr - cd->mcodebase);
#endif
					M_ILD32(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, baseval));

					/* 				if (s1 != REG_ITMP1) { */
					/* 					emit_mov_membase_reg(cd, REG_ITMP3, OFFSET(vftbl_t, baseval), REG_ITMP1); */
					/* 					emit_mov_membase_reg(cd, REG_ITMP3, OFFSET(vftbl_t, diffval), REG_ITMP3); */
					/* #if defined(ENABLE_THREADS) */
					/* 					codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase); */
					/* #endif */
					/* 					emit_alu_reg_reg(cd, ALU_SUB, REG_ITMP1, REG_ITMP2); */

					/* 				} else { */
					M_ILD32(REG_ITMP3, REG_ITMP3, OFFSET(vftbl_t, baseval));
					M_ISUB(REG_ITMP3, REG_ITMP2);
					M_MOV_IMM(supervftbl, REG_ITMP3);
					M_ILD(REG_ITMP3, REG_ITMP3, OFFSET(vftbl_t, diffval));
#if defined(ENABLE_THREADS)
					codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
					/* 				} */

					M_CMP(REG_ITMP3, REG_ITMP2);
					M_BA(0);         /* (u) REG_ITMP2 > (u) REG_ITMP3 -> jump */
					codegen_add_classcastexception_ref(cd, s1);
				}

				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP3);
			}
			else {
				/* array type cast-check */

				s1 = emit_load_s1(jd, iptr, src, REG_ITMP2);
				M_AST(s1, REG_SP, 0 * 4);

				if (iptr->val.a == NULL) {
					codegen_addpatchref(cd, PATCHER_builtin_arraycheckcast,
										iptr->target, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				M_AST_IMM(iptr->val.a, REG_SP, 1 * 4);
				M_MOV_IMM(BUILTIN_arraycheckcast, REG_ITMP3);
				M_CALL(REG_ITMP3);

				s1 = emit_load_s1(jd, iptr, src, REG_ITMP2);
				M_TEST(REG_RESULT);
				M_BEQ(0);
				codegen_add_classcastexception_ref(cd, s1);

				d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, s1);
			}

			M_INTMOVE(s1, d);
			emit_store(jd, iptr, iptr->dst, d);
			break;

		case ICMD_INSTANCEOF: /* ..., objectref ==> ..., intresult            */
		                      /* op1:   0 == array, 1 == class                */
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

			super = (classinfo *) iptr->val.a;

			if (!super) {
				superindex = 0;
				supervftbl = NULL;

			} else {
				superindex = super->index;
				supervftbl = super->vftbl;
			}
			
#if defined(ENABLE_THREADS)
			codegen_threadcritrestart(cd, cd->mcodeptr - cd->mcodebase);
#endif

			s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);
			d = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_ITMP2);
			if (s1 == d) {
				M_INTMOVE(s1, REG_ITMP1);
				s1 = REG_ITMP1;
			}

			/* calculate interface instanceof code size */

			s2 = 2; /* mov_membase_reg */
			CALCOFFSETBYTES(s2, s1, OFFSET(java_objectheader, vftbl));

			s2 += (2 + 4 /* mov_membase32_reg */ + 2 + 4 /* alu_imm32_reg */ +
				   2 /* test */ + 6 /* jcc */ + 2 + 4 /* mov_membase32_reg */ +
				   2 /* test */ + 6 /* jcc */ + 5 /* mov_imm_reg */);

			if (!super)
				s2 += (opt_showdisassemble ? 5 : 0);

			/* calculate class instanceof code size */

			s3 = 2; /* mov_membase_reg */
			CALCOFFSETBYTES(s3, s1, OFFSET(java_objectheader, vftbl));
			s3 += 5; /* mov_imm_reg */
			s3 += 2;
			CALCOFFSETBYTES(s3, REG_ITMP1, OFFSET(vftbl_t, baseval));
			s3 += 2;
			CALCOFFSETBYTES(s3, REG_ITMP2, OFFSET(vftbl_t, diffval));
			s3 += 2;
			CALCOFFSETBYTES(s3, REG_ITMP2, OFFSET(vftbl_t, baseval));

			s3 += (2 /* alu_reg_reg */ + 2 /* alu_reg_reg */ +
				   2 /* alu_reg_reg */ + 6 /* jcc */ + 5 /* mov_imm_reg */);

			if (!super)
				s3 += (opt_showdisassemble ? 5 : 0);

			M_CLR(d);

			/* if class is not resolved, check which code to call */

			if (!super) {
				M_TEST(s1);
				M_BEQ(5 + (opt_showdisassemble ? 5 : 0) + 6 + 6 + s2 + 5 + s3);

				codegen_addpatchref(cd, PATCHER_checkcast_instanceof_flags,
									(constant_classref *) iptr->target, 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				M_MOV_IMM(0, REG_ITMP3);                      /* super->flags */
				M_AND_IMM32(ACC_INTERFACE, REG_ITMP3);
				M_BEQ(s2 + 5);
			}

			/* interface instanceof code */

			if (!super || (super->flags & ACC_INTERFACE)) {
				if (super) {
					M_TEST(s1);
					M_BEQ(s2);
				}

				M_ALD(REG_ITMP1, s1, OFFSET(java_objectheader, vftbl));

				if (!super) {
					codegen_addpatchref(cd,
										PATCHER_checkcast_instanceof_interface,
										(constant_classref *) iptr->target, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				M_ILD32(REG_ITMP3,
						REG_ITMP1, OFFSET(vftbl_t, interfacetablelength));
				M_ISUB_IMM32(superindex, REG_ITMP3);
				M_TEST(REG_ITMP3);

				disp = (2 + 4 /* mov_membase32_reg */ + 2 /* test */ +
						6 /* jcc */ + 5 /* mov_imm_reg */);

				M_BLE(disp);
				M_ALD32(REG_ITMP1, REG_ITMP1,
						OFFSET(vftbl_t, interfacetable[0]) -
						superindex * sizeof(methodptr*));
				M_TEST(REG_ITMP1);
/*  					emit_setcc_reg(cd, CC_A, d); */
/*  					emit_jcc(cd, CC_BE, 5); */
				M_BEQ(5);
				M_MOV_IMM(1, d);

				if (!super)
					M_JMP_IMM(s3);
			}

			/* class instanceof code */

			if (!super || !(super->flags & ACC_INTERFACE)) {
				if (super) {
					M_TEST(s1);
					M_BEQ(s3);
				}

				M_ALD(REG_ITMP1, s1, OFFSET(java_objectheader, vftbl));

				if (!super) {
					codegen_addpatchref(cd, PATCHER_instanceof_class,
										(constant_classref *) iptr->target, 0);

					if (opt_showdisassemble) {
						M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
					}
				}

				M_MOV_IMM(supervftbl, REG_ITMP2);
#if defined(ENABLE_THREADS)
				codegen_threadcritstart(cd, cd->mcodeptr - cd->mcodebase);
#endif
				M_ILD(REG_ITMP1, REG_ITMP1, OFFSET(vftbl_t, baseval));
				M_ILD(REG_ITMP3, REG_ITMP2, OFFSET(vftbl_t, diffval));
				M_ILD(REG_ITMP2, REG_ITMP2, OFFSET(vftbl_t, baseval));
#if defined(ENABLE_THREADS)
				codegen_threadcritstop(cd, cd->mcodeptr - cd->mcodebase);
#endif
				M_ISUB(REG_ITMP2, REG_ITMP1);
				M_CLR(d);                                 /* may be REG_ITMP2 */
				M_CMP(REG_ITMP3, REG_ITMP1);
				M_BA(5);
				M_MOV_IMM(1, d);
			}
  			emit_store(jd, iptr, iptr->dst, d);
			}
			break;

			break;

		case ICMD_MULTIANEWARRAY:/* ..., cnt1, [cnt2, ...] ==> ..., arrayref  */
		                      /* op1 = dimension, val.a = class               */

			/* check for negative sizes and copy sizes to stack if necessary  */

  			MCODECHECK((iptr->op1 << 1) + 64);

			for (s1 = iptr->op1; --s1 >= 0; src = src->prev) {
				/* copy SAVEDVAR sizes to stack */

				if (src->varkind != ARGVAR) {
					if (src->flags & INMEMORY) {
						M_ILD(REG_ITMP1, REG_SP, src->regoff * 4);
						M_IST(REG_ITMP1, REG_SP, (s1 + 3) * 4);
					}
					else
						M_IST(src->regoff, REG_SP, (s1 + 3) * 4);
				}
			}

			/* is a patcher function set? */

			if (iptr->val.a == NULL) {
				codegen_addpatchref(cd, PATCHER_builtin_multianewarray,
									(constant_classref *) iptr->target, 0);

				if (opt_showdisassemble) {
					M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
				}

				disp = 0;

			}
			else
				disp = (ptrint) iptr->val.a;

			/* a0 = dimension count */

			M_IST_IMM(iptr->op1, REG_SP, 0 * 4);

			/* a1 = arraydescriptor */

			M_IST_IMM(disp, REG_SP, 1 * 4);

			/* a2 = pointer to dimensions = stack pointer */

			M_MOV(REG_SP, REG_ITMP1);
			M_AADD_IMM(3 * 4, REG_ITMP1);
			M_AST(REG_ITMP1, REG_SP, 2 * 4);

			M_MOV_IMM(BUILTIN_multianewarray, REG_ITMP1);
			M_CALL(REG_ITMP1);

			/* check for exception before result assignment */

			M_TEST(REG_RESULT);
			M_BEQ(0);
			codegen_add_fillinstacktrace_ref(cd);

			s1 = codegen_reg_of_var(rd, iptr->opc, iptr->dst, REG_RESULT);
			M_INTMOVE(REG_RESULT, s1);
			emit_store(jd, iptr, iptr->dst, s1);
			break;

		default:
			*exceptionptr =
				new_internalerror("Unknown ICMD %d", iptr->opc);
			return false;
	} /* switch */
		
	} /* for instruction */
		
	/* copy values to interface registers */

	src = bptr->outstack;
	len = bptr->outdepth;
	MCODECHECK(64+len);
#if defined(ENABLE_LSRA)
	if (!opt_lsra)
#endif
	while (src) {
		len--;
		if ((src->varkind != STACKVAR)) {
			s2 = src->type;
			if (IS_FLT_DBL_TYPE(s2)) {
				s1 = emit_load_s1(jd, iptr, src, REG_FTMP1);
				if (!(rd->interfaces[len][s2].flags & INMEMORY))
					M_FLTMOVE(s1, rd->interfaces[len][s2].regoff);
				else
					M_DST(s1, REG_SP, rd->interfaces[len][s2].regoff * 4);

			} else {
				if (IS_2_WORD_TYPE(s2))
					assert(0);
/* 					s1 = emit_load_s1(jd, iptr, src, PACK_REGS(REG_ITMP1, REG_ITMP2)); */
				else
					s1 = emit_load_s1(jd, iptr, src, REG_ITMP1);

				if (!(rd->interfaces[len][s2].flags & INMEMORY)) {
					if (IS_2_WORD_TYPE(s2))
						M_LNGMOVE(s1, rd->interfaces[len][s2].regoff);
					else
						M_INTMOVE(s1, rd->interfaces[len][s2].regoff);

				} else {
					if (IS_2_WORD_TYPE(s2))
						M_LST(s1, REG_SP, rd->interfaces[len][s2].regoff * 4);
					else
						M_IST(s1, REG_SP, rd->interfaces[len][s2].regoff * 4);
				}
			}
		}
		src = src->prev;
	}

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


	/* generate exception and patcher stubs */

	{
		exceptionref *eref;
		patchref     *pref;
		u8            mcode;
		u1           *savedmcodeptr;
		u1           *tmpmcodeptr;

		savedmcodeptr = NULL;

		/* generate exception stubs */

		for (eref = cd->exceptionrefs; eref != NULL; eref = eref->next) {
			gen_resolvebranch(cd->mcodebase + eref->branchpos,
							  eref->branchpos,
							  cd->mcodeptr - cd->mcodebase);

			MCODECHECK(512);

			/* Check if the exception is an
			   ArrayIndexOutOfBoundsException.  If so, move index register
			   into REG_ITMP1. */

			if (eref->reg != -1)
				M_INTMOVE(eref->reg, REG_ITMP1);

			/* calcuate exception address */

			M_MOV_IMM(0, REG_ITMP2_XPC);
			dseg_adddata(cd);
			M_AADD_IMM32(eref->branchpos - 6, REG_ITMP2_XPC);

			/* move function to call into REG_ITMP3 */

			M_MOV_IMM(eref->function, REG_ITMP3);

			if (savedmcodeptr != NULL) {
				M_JMP_IMM((savedmcodeptr - cd->mcodeptr) - 5);

			} else {
				savedmcodeptr = cd->mcodeptr;

				M_ASUB_IMM(5 * 4, REG_SP);

				/* first save REG_ITMP1 so we can use it */

				M_AST(REG_ITMP1, REG_SP, 4 * 4);                /* for AIOOBE */

				M_AST_IMM(0, REG_SP, 0 * 4);
				dseg_adddata(cd);
				M_MOV(REG_SP, REG_ITMP1);
				M_AADD_IMM(5 * 4, REG_ITMP1);
				M_AST(REG_ITMP1, REG_SP, 1 * 4);
				M_ALD(REG_ITMP1, REG_SP, (5 + stackframesize) * 4);
				M_AST(REG_ITMP1, REG_SP, 2 * 4);
				M_AST(REG_ITMP2_XPC, REG_SP, 3 * 4);

				M_CALL(REG_ITMP3);

				M_ALD(REG_ITMP2_XPC, REG_SP, 3 * 4);
				M_AADD_IMM(5 * 4, REG_SP);

				M_MOV_IMM(asm_handle_exception, REG_ITMP3);
				M_JMP(REG_ITMP3);
			}
		}


		/* generate code patching stub call code */

		for (pref = cd->patchrefs; pref != NULL; pref = pref->next) {
			/* check code segment size */

			MCODECHECK(512);

			/* Get machine code which is patched back in later. A
			   `call rel32' is 5 bytes long. */

			savedmcodeptr = cd->mcodebase + pref->branchpos;
			mcode = *((u8 *) savedmcodeptr);

			/* patch in `call rel32' to call the following code */

			tmpmcodeptr  = cd->mcodeptr;    /* save current mcodeptr          */
			cd->mcodeptr = savedmcodeptr;   /* set mcodeptr to patch position */

			M_CALL_IMM(tmpmcodeptr - (savedmcodeptr + PATCHER_CALL_SIZE));

			cd->mcodeptr = tmpmcodeptr;     /* restore the current mcodeptr   */

			/* save REG_ITMP3 */

			M_PUSH(REG_ITMP3);

			/* move pointer to java_objectheader onto stack */

#if defined(ENABLE_THREADS)
			(void) dseg_addaddress(cd, NULL);                         /* flcword    */
			(void) dseg_addaddress(cd, lock_get_initial_lock_word()); /* monitorPtr */
			off = dseg_addaddress(cd, NULL);                          /* vftbl      */

			M_MOV_IMM(0, REG_ITMP3);
			dseg_adddata(cd);
			M_AADD_IMM(off, REG_ITMP3);
			M_PUSH(REG_ITMP3);
#else
			M_PUSH_IMM(0);
#endif

			/* move machine code bytes and classinfo pointer into registers */

			M_PUSH_IMM(mcode >> 32);
			M_PUSH_IMM(mcode);
			M_PUSH_IMM(pref->ref);
			M_PUSH_IMM(pref->patcher);

			M_MOV_IMM(asm_patcher_wrapper, REG_ITMP3);
			M_JMP(REG_ITMP3);
		}
	}

	/* generate replacement-out stubs */

	{
		int i;

		replacementpoint = jd->code->rplpoints;

		for (i = 0; i < jd->code->rplpointcount; ++i, ++replacementpoint) {
			/* check code segment size */

			MCODECHECK(512);

			/* note start of stub code */

			replacementpoint->outcode = (u1*) (ptrint)(cd->mcodeptr - cd->mcodebase);

			/* make machine code for patching */

			disp = (ptrint)(replacementpoint->outcode - replacementpoint->pc) - 5;
			replacementpoint->mcode = 0xe9 | ((u8)disp << 8);

			/* push address of `rplpoint` struct */
			
			M_PUSH_IMM(replacementpoint);

			/* jump to replacement function */

			M_PUSH_IMM(asm_replacement_out);
			M_RET;
		}
	}
	
	codegen_finish(jd);

	/* everything's ok */

	return true;
}


/* createcompilerstub **********************************************************

   Creates a stub routine which calls the compiler.
	
*******************************************************************************/

#define COMPILERSTUB_DATASIZE    3 * SIZEOF_VOID_P
#define COMPILERSTUB_CODESIZE    12

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

	/* code for the stub */

	M_MOV_IMM(m, REG_ITMP1);            /* method info                        */
	M_MOV_IMM(asm_call_jit_compiler, REG_ITMP3);
	M_JMP(REG_ITMP3);

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
	methoddesc   *md;
	s4            stackframesize;
	s4            nativeparams;
	s4            i, j;                 /* count variables                    */
	s4            t;
	s4            s1, s2, disp;

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
		1 +                             /* function pointer                   */
		4 * 4 +                         /* 4 arguments (start_native_call)    */
		nmd->memuse;

	/* create method header */

	(void) dseg_addaddress(cd, code);                      /* CodeinfoPointer */
	(void) dseg_adds4(cd, stackframesize * 4);             /* FrameSize       */
	(void) dseg_adds4(cd, 0);                              /* IsSync          */
	(void) dseg_adds4(cd, 0);                              /* IsLeaf          */
	(void) dseg_adds4(cd, 0);                              /* IntSave         */
	(void) dseg_adds4(cd, 0);                              /* FltSave         */
	(void) dseg_addlinenumbertablesize(cd);
	(void) dseg_adds4(cd, 0);                              /* ExTableSize     */

	/* generate native method profiling code */

	if (JITDATA_HAS_FLAG_INSTRUMENT(jd)) {
		/* count frequency */

		M_MOV_IMM(code, REG_ITMP1);
		M_IADD_IMM_MEMBASE(1, REG_ITMP1, OFFSET(codeinfo, frequency));
	}

	/* calculate stackframe size for native function */

	M_ASUB_IMM(stackframesize * 4, REG_SP);

#if !defined(NDEBUG)
	if (opt_verbosecall) {
		s4 p, t;

		disp = stackframesize * 4;

		M_ASUB_IMM(TRACE_ARGS_NUM * 8 + 4, REG_SP);
    
		for (p = 0; p < md->paramcount && p < TRACE_ARGS_NUM; p++) {
			t = md->paramtypes[p].type;
			if (IS_INT_LNG_TYPE(t)) {
				if (IS_2_WORD_TYPE(t)) {
					M_ILD(REG_ITMP1, REG_SP,
						  4 + TRACE_ARGS_NUM * 8 + 4 + disp);
					M_ILD(REG_ITMP2, REG_SP,
						  4 + TRACE_ARGS_NUM * 8 + 4 + disp + 4);
					M_IST(REG_ITMP1, REG_SP, p * 8);
					M_IST(REG_ITMP2, REG_SP, p * 8 + 4);

				} else if (t == TYPE_ADR) {
					M_ALD(REG_ITMP1, REG_SP,
						  4 + TRACE_ARGS_NUM * 8 + 4 + disp);
					M_CLR(REG_ITMP2);
					M_AST(REG_ITMP1, REG_SP, p * 8);
					M_AST(REG_ITMP2, REG_SP, p * 8 + 4);

				} else {
					M_ILD(EAX, REG_SP, 4 + TRACE_ARGS_NUM * 8 + 4 + disp);
					emit_cltd(cd);
					M_IST(EAX, REG_SP, p * 8);
					M_IST(EDX, REG_SP, p * 8 + 4);
				}

			} else {
				if (!IS_2_WORD_TYPE(t)) {
					emit_flds_membase(cd, REG_SP,
									  4 + TRACE_ARGS_NUM * 8 + 4 + disp);
					emit_fstps_membase(cd, REG_SP, p * 8);
					emit_alu_reg_reg(cd, ALU_XOR, REG_ITMP2, REG_ITMP2);
					M_IST(REG_ITMP2, REG_SP, p * 8 + 4);

				} else {
					emit_fldl_membase(cd, REG_SP,
					    4 + TRACE_ARGS_NUM * 8 + 4 + disp);
					emit_fstpl_membase(cd, REG_SP, p * 8);
				}
			}
			disp += (IS_2_WORD_TYPE(t)) ? 8 : 4;
		}
	
		M_CLR(REG_ITMP1);
		for (p = md->paramcount; p < TRACE_ARGS_NUM; p++) {
			M_IST(REG_ITMP1, REG_SP, p * 8);
			M_IST(REG_ITMP1, REG_SP, p * 8 + 4);
		}

		M_AST_IMM(m, REG_SP, TRACE_ARGS_NUM * 8);

		M_MOV_IMM(builtin_trace_args, REG_ITMP1);
		M_CALL(REG_ITMP1);

		M_AADD_IMM(TRACE_ARGS_NUM * 8 + 4, REG_SP);
	}
#endif /* !defined(NDEBUG) */

	/* get function address (this must happen before the stackframeinfo) */

#if !defined(WITH_STATIC_CLASSPATH)
	if (f == NULL) {
		codegen_addpatchref(cd, PATCHER_resolve_native, m, 0);

		if (opt_showdisassemble) {
			M_NOP; M_NOP; M_NOP; M_NOP; M_NOP;
		}
	}
#endif

	M_AST_IMM((ptrint) f, REG_SP, 4 * 4);

	/* Mark the whole fpu stack as free for native functions (only for saved  */
	/* register count == 0).                                                  */

	emit_ffree_reg(cd, 0);
	emit_ffree_reg(cd, 1);
	emit_ffree_reg(cd, 2);
	emit_ffree_reg(cd, 3);
	emit_ffree_reg(cd, 4);
	emit_ffree_reg(cd, 5);
	emit_ffree_reg(cd, 6);
	emit_ffree_reg(cd, 7);

	/* prepare data structures for native function call */

	M_MOV(REG_SP, REG_ITMP1);
	M_AADD_IMM(stackframesize * 4, REG_ITMP1);

	M_AST(REG_ITMP1, REG_SP, 0 * 4);
	M_IST_IMM(0, REG_SP, 1 * 4);
	dseg_adddata(cd);

	M_MOV(REG_SP, REG_ITMP2);
	M_AADD_IMM(stackframesize * 4 + SIZEOF_VOID_P, REG_ITMP2);

	M_AST(REG_ITMP2, REG_SP, 2 * 4);
	M_ALD(REG_ITMP3, REG_SP, stackframesize * 4);
	M_AST(REG_ITMP3, REG_SP, 3 * 4);
	M_MOV_IMM(codegen_start_native_call, REG_ITMP1);
	M_CALL(REG_ITMP1);

	M_ALD(REG_ITMP3, REG_SP, 4 * 4);

	/* copy arguments into new stackframe */

	for (i = md->paramcount - 1, j = i + nativeparams; i >= 0; i--, j--) {
		t = md->paramtypes[i].type;

		if (!md->params[i].inmemory) {
			/* no integer argument registers */
		} else {       /* float/double in memory can be copied like int/longs */
			s1 = (md->params[i].regoff + stackframesize + 1) * 4;
			s2 = nmd->params[j].regoff * 4;

			M_ILD(REG_ITMP1, REG_SP, s1);
			M_IST(REG_ITMP1, REG_SP, s2);
			if (IS_2_WORD_TYPE(t)) {
				M_ILD(REG_ITMP1, REG_SP, s1 + 4);
				M_IST(REG_ITMP1, REG_SP, s2 + 4);
			}
		}
	}

	/* if function is static, put class into second argument */

	if (m->flags & ACC_STATIC)
		M_AST_IMM((ptrint) m->class, REG_SP, 1 * 4);

	/* put env into first argument */

	M_AST_IMM((ptrint) _Jv_env, REG_SP, 0 * 4);

	/* call the native function */

	M_CALL(REG_ITMP3);

	/* save return value */

	if (md->returntype.type != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(md->returntype.type)) {
			if (IS_2_WORD_TYPE(md->returntype.type))
				M_IST(REG_RESULT2, REG_SP, 2 * 4);
			M_IST(REG_RESULT, REG_SP, 1 * 4);
		}
		else {
			if (IS_2_WORD_TYPE(md->returntype.type))
				emit_fstl_membase(cd, REG_SP, 1 * 4);
			else
				emit_fsts_membase(cd, REG_SP, 1 * 4);
		}
	}

#if !defined(NDEBUG)
    if (opt_verbosecall) {
		/* restore return value */

		if (IS_INT_LNG_TYPE(md->returntype.type)) {
			if (IS_2_WORD_TYPE(md->returntype.type))
				M_ILD(REG_RESULT2, REG_SP, 2 * 4);
			M_ILD(REG_RESULT, REG_SP, 1 * 4);
	
		} else {
			if (IS_2_WORD_TYPE(md->returntype.type))
				emit_fldl_membase(cd, REG_SP, 1 * 4);
			else
				emit_flds_membase(cd, REG_SP, 1 * 4);
		}

		M_ASUB_IMM(4 + 8 + 8 + 4, REG_SP);

		M_AST_IMM((ptrint) m, REG_SP, 0);

		M_IST(REG_RESULT, REG_SP, 4);
		M_IST(REG_RESULT2, REG_SP, 4 + 4);

		emit_fstl_membase(cd, REG_SP, 4 + 8);
		emit_fsts_membase(cd, REG_SP, 4 + 8 + 8);

		M_MOV_IMM(builtin_displaymethodstop, REG_ITMP1);
		M_CALL(REG_ITMP1);

		M_AADD_IMM(4 + 8 + 8 + 4, REG_SP);
    }
#endif /* !defined(NDEBUG) */

	/* remove native stackframe info */

	M_MOV(REG_SP, REG_ITMP1);
	M_AADD_IMM(stackframesize * 4, REG_ITMP1);

	M_AST(REG_ITMP1, REG_SP, 0 * 4);
	M_MOV_IMM(codegen_finish_native_call, REG_ITMP1);
	M_CALL(REG_ITMP1);
	M_MOV(REG_RESULT, REG_ITMP2);                 /* REG_ITMP3 == REG_RESULT2 */

	/* restore return value */

	if (md->returntype.type != TYPE_VOID) {
		if (IS_INT_LNG_TYPE(md->returntype.type)) {
			if (IS_2_WORD_TYPE(md->returntype.type))
				M_ILD(REG_RESULT2, REG_SP, 2 * 4);
			M_ILD(REG_RESULT, REG_SP, 1 * 4);
		}
		else {
			if (IS_2_WORD_TYPE(md->returntype.type))
				emit_fldl_membase(cd, REG_SP, 1 * 4);
			else
				emit_flds_membase(cd, REG_SP, 1 * 4);
		}
	}

	M_AADD_IMM(stackframesize * 4, REG_SP);

	/* check for exception */

	M_TEST(REG_ITMP2);
	M_BNE(1);

	M_RET;

	/* handle exception */

	M_MOV(REG_ITMP2, REG_ITMP1_XPTR);
	M_ALD(REG_ITMP2_XPC, REG_SP, 0);
	M_ASUB_IMM(2, REG_ITMP2_XPC);

	M_MOV_IMM(asm_handle_nat_exception, REG_ITMP3);
	M_JMP(REG_ITMP3);


	/* process patcher calls **************************************************/

	{
		u1       *xcodeptr;
		patchref *pref;
		u8        mcode;
		u1       *tmpmcodeptr;

		for (pref = cd->patchrefs; pref != NULL; pref = pref->next) {
			/* Get machine code which is patched back in later. A
			   `call rel32' is 5 bytes long. */

			xcodeptr = cd->mcodebase + pref->branchpos;
			mcode =  *((u8 *) xcodeptr);

			/* patch in `call rel32' to call the following code */

			tmpmcodeptr  = cd->mcodeptr;    /* save current mcodeptr          */
			cd->mcodeptr = xcodeptr;        /* set mcodeptr to patch position */

			M_CALL_IMM(tmpmcodeptr - (xcodeptr + PATCHER_CALL_SIZE));

			cd->mcodeptr = tmpmcodeptr;     /* restore the current mcodeptr   */

			/* save REG_ITMP3 */

			M_PUSH(REG_ITMP3);

			/* move pointer to java_objectheader onto stack */

#if defined(ENABLE_THREADS)
			/* create a virtual java_objectheader */

			(void) dseg_addaddress(cd, NULL);                         /* flcword    */
			(void) dseg_addaddress(cd, lock_get_initial_lock_word()); /* monitorPtr */
			disp = dseg_addaddress(cd, NULL);                         /* vftbl      */

			M_MOV_IMM(0, REG_ITMP3);
			dseg_adddata(cd);
			M_AADD_IMM(disp, REG_ITMP3);
			M_PUSH(REG_ITMP3);
#else
			M_PUSH_IMM(0);
#endif

			/* move machine code bytes and classinfo pointer onto stack */

			M_PUSH_IMM((mcode >> 32));
			M_PUSH_IMM(mcode);
			M_PUSH_IMM(pref->ref);
			M_PUSH_IMM(pref->patcher);

			M_MOV_IMM(asm_patcher_wrapper, REG_ITMP3);
			M_JMP(REG_ITMP3);
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
