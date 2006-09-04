/* src/vm/jit/powerpc64/emit.c - PowerPC code emitter functions

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

   Authors: Christian Thalinger

   Changes:

   $Id: emitfuncs.c 4398 2006-01-31 23:43:08Z twisti $

*/


#include "config.h"

#include <assert.h>

#include "vm/types.h"

#include "md-abi.h"

#include "vm/jit/emit.h"
#include "vm/jit/jit.h"
#include "vm/jit/powerpc64/codegen.h"
#include "vm/builtin.h"


/* code generation functions **************************************************/

/* emit_load_s1 ****************************************************************

   Emits a possible load of the first source operand.

*******************************************************************************/

s4 emit_load_s1(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		if (IS_FLT_DBL_TYPE(src->type)) {
			if (IS_2_WORD_TYPE(src->type))
				M_DLD(tempreg, REG_SP, disp);
			else
				M_FLD(tempreg, REG_SP, disp);

		} else {
			if (IS_2_WORD_TYPE(src->type))
				M_LLD(tempreg, REG_SP, disp);
			else
				M_ILD(tempreg, REG_SP, disp);
		}

		reg = tempreg;
	} else
		reg = src->regoff;

	return reg;
}


/* emit_load_s2 ****************************************************************

   Emits a possible load of the second source operand.

*******************************************************************************/

s4 emit_load_s2(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		if (IS_FLT_DBL_TYPE(src->type)) {
			if (IS_2_WORD_TYPE(src->type))
				M_DLD(tempreg, REG_SP, disp);
			else
				M_FLD(tempreg, REG_SP, disp);

		} else {
			if (IS_2_WORD_TYPE(src->type))
				M_LLD(tempreg, REG_SP, disp);
			else
				M_ILD(tempreg, REG_SP, disp);
		}

		reg = tempreg;
	} else
		reg = src->regoff;

	return reg;
}


/* emit_load_s3 ****************************************************************

   Emits a possible load of the third source operand.

*******************************************************************************/

s4 emit_load_s3(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		if (IS_FLT_DBL_TYPE(src->type)) {
			if (IS_2_WORD_TYPE(src->type))
				M_DLD(tempreg, REG_SP, disp);
			else
				M_FLD(tempreg, REG_SP, disp);

		} else {
			if (IS_2_WORD_TYPE(src->type))
				M_LLD(tempreg, REG_SP, disp);
			else
				M_ILD(tempreg, REG_SP, disp);
		}

		reg = tempreg;
	} else
		reg = src->regoff;

	return reg;
}


/* emit_load_s1_low ************************************************************

   Emits a possible load of the low 32-bits of the first long source
   operand.

*******************************************************************************/

s4 emit_load_s1_low(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp + 4);

		reg = tempreg;
	} else
		reg = GET_LOW_REG(src->regoff);

	return reg;
}


/* emit_load_s2_low ************************************************************

   Emits a possible load of the low 32-bits of the second long source
   operand.

*******************************************************************************/

s4 emit_load_s2_low(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp + 4);

		reg = tempreg;
	} else
		reg = GET_LOW_REG(src->regoff);

	return reg;
}


/* emit_load_s3_low ************************************************************

   Emits a possible load of the low 32-bits of the third long source
   operand.

*******************************************************************************/

s4 emit_load_s3_low(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp + 4);

		reg = tempreg;
	} else
		reg = GET_LOW_REG(src->regoff);

	return reg;
}


/* emit_load_s1_high ***********************************************************

   Emits a possible load of the high 32-bits of the first long source
   operand.

*******************************************************************************/

s4 emit_load_s1_high(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp);

		reg = tempreg;
	} else
		reg = GET_HIGH_REG(src->regoff);

	return reg;
}


/* emit_load_s2_high ***********************************************************

   Emits a possible load of the high 32-bits of the second long source
   operand.

*******************************************************************************/

s4 emit_load_s2_high(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp);

		reg = tempreg;
	} else
		reg = GET_HIGH_REG(src->regoff);

	return reg;
}


/* emit_load_s3_high ***********************************************************

   Emits a possible load of the high 32-bits of the third long source
   operand.

*******************************************************************************/

s4 emit_load_s3_high(jitdata *jd, instruction *iptr, stackptr src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	assert(src->type == TYPE_LNG);

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->regoff * 4;

		M_ILD(tempreg, REG_SP, disp);

		reg = tempreg;
	} else
		reg = GET_HIGH_REG(src->regoff);

	return reg;
}


/* emit_store ******************************************************************

   XXX

*******************************************************************************/

void emit_store(jitdata *jd, instruction *iptr, stackptr dst, s4 d)
{
	codegendata  *cd;

	/* get required compiler data */

	cd = jd->cd;

	if (dst->flags & INMEMORY) {
		COUNT_SPILLS;

		if (IS_FLT_DBL_TYPE(dst->type)) {
			if (IS_2_WORD_TYPE(dst->type))
				M_DST(d, REG_SP, dst->regoff * 4);
			else
				M_FST(d, REG_SP, dst->regoff * 4);

		} else {
			if (IS_2_WORD_TYPE(dst->type))
				M_LST(d, REG_SP, dst->regoff * 4);
			else
				M_IST(d, REG_SP, dst->regoff * 4);
		}
	}
}


/* emit_copy *******************************************************************

   XXX

*******************************************************************************/

void emit_copy(jitdata *jd, instruction *iptr, stackptr src, stackptr dst)
{
	codegendata  *cd;
	registerdata *rd;
	s4            s1, d;

	/* get required compiler data */

	cd = jd->cd;
	rd = jd->rd;

	if (src->type == TYPE_LNG)
		d = codegen_reg_of_var(rd, iptr->opc, dst, PACK_REGS(REG_ITMP2, REG_ITMP1));
	else
		d = codegen_reg_of_var(rd, iptr->opc, dst, REG_IFTMP);

	if ((src->regoff != dst->regoff) ||
		((src->flags ^ dst->flags) & INMEMORY)) {
		s1 = emit_load_s1(jd, iptr, src, d);

		if (s1 != d) {
			if (IS_FLT_DBL_TYPE(src->type))
				M_FMOV(s1, d);
			else {
				if (IS_2_WORD_TYPE(src->type)) {
					M_MOV(GET_LOW_REG(s1), GET_LOW_REG(d));
					M_MOV(GET_HIGH_REG(s1), GET_HIGH_REG(d));
                } else
                    M_MOV(s1, d);
			}
		}

		emit_store(jd, iptr, dst, d);
	}
}


/* emit_iconst *****************************************************************

   XXX

*******************************************************************************/

void emit_iconst(codegendata *cd, s4 d, s4 value)
{
	s4 disp;

	if ((value >= -32768) && (value <= 32767))
		M_LDA_INTERN(d, REG_ZERO, value);
	else {
		disp = dseg_adds4(cd, value);
		M_ILD(d, REG_PV, disp);
	}
}

/* emit_verbosecall_enter ******************************************************
 *
 *    Generates the code for the call trace.
 *
 ********************************************************************************/
void emit_verbosecall_enter (jitdata *jd)
{
	methodinfo   *m;
	codegendata  *cd;
	registerdata *rd;
	s4 s1, p, t, d;
/*	int stack_off; */
	int stack_size;
	methoddesc *md;

	/* get required compiler data */

	m  = jd->m;
	cd = jd->cd;
	rd = jd->rd;

	md = m->parseddesc;
	
	/* Build up Stackframe for builtin_trace_args call (a multiple of 16) */
	/* For Darwin:                                                        */
	/* TODO                                                               */
	/* For Linux:                                                         */
	/* setup stack for TRACE_ARGS_NUM registers                           */
	/* == LA_SIZE + PA_SIZE + 8 (methodinfo argument) + TRACE_ARGS_NUM*8 + 8 (itmp1)              */
	
	/* in nativestubs no Place to save the LR (Link Register) would be needed */
	/* but since the stack frame has to be aligned the 4 Bytes would have to  */
	/* be padded again */

#if defined(__DARWIN__)
	stack_size = LA_SIZE + (TRACE_ARGS_NUM + 1) * 8;
#else
	stack_size = LA_SIZE + PA_SIZE + 8 + TRACE_ARGS_NUM * 8 + 8;
#endif

	/* mark trace code */
	M_NOP;

	/* save up to TRACE_ARGS_NUM arguments into the reserved stack space */
#if 0
#if defined(__DARWIN__)
	/* Copy Params starting from first to Stack                          */
	/* since TRACE_ARGS == INT_ARG_CNT all used integer argument regs    */ 
	/* are saved                                                         */
	p = 0;
#else
	/* Copy Params starting from fifth to Stack (INT_ARG_CNT/2) are in   */
	/* integer argument regs                                             */
	/* all integer argument registers have to be saved                   */
	for (p = 0; p < 8; p++) {
		d = rd->argintregs[p];
		/* save integer argument registers */
		M_LST(d, REG_SP, LA_SIZE + PA_SIZE + 4 * 8 + 8 + p * 8);
	}
	p = 4;
#endif
#endif
	M_MFLR(REG_ZERO);
	M_AST(REG_ZERO, REG_SP, LA_LR_OFFSET);
	M_STDU(REG_SP, REG_SP, -stack_size);

	for (p = 0; p < md->paramcount && p < TRACE_ARGS_NUM; p++) {
		t = md->paramtypes[p].type;
		if (IS_INT_LNG_TYPE(t)) {
			if (!md->params[p].inmemory) { /* Param in Arg Reg */
				M_LST(rd->argintregs[md->params[p].regoff], REG_SP, LA_SIZE + PA_SIZE + 8 + p * 8);
			} else { /* Param on Stack */
				s1 = (md->params[p].regoff + cd->stackframesize) * 8 + stack_size;
				M_LLD(REG_ITMP2, REG_SP, s1);
				M_LST(REG_ITMP2, REG_SP, LA_SIZE + PA_SIZE + 8 + p * 8);
			}
		} else { /* IS_FLT_DBL_TYPE(t) */
			if (!md->params[p].inmemory) { /* in Arg Reg */
				s1 = rd->argfltregs[md->params[p].regoff];
				M_DST(s1, REG_SP, LA_SIZE + PA_SIZE + 8 + p * 8);
			} else { /* on Stack */
				/* this should not happen */
				assert(0);
			}
		}
	}

	/* load first 4 (==INT_ARG_CNT/2) arguments into integer registers */
#if defined(__DARWIN__)
	for (p = 0; p < 8; p++) {
		d = rd->argintregs[p];
		M_ILD(d, REG_SP, LA_SIZE + p * 4);
	}
#else
	/* LINUX */
	/* Set integer and float argument registers for trace_args call */
	/* offset to saved integer argument registers                   */
	for (p = 0; (p < TRACE_ARGS_NUM) && (p < md->paramcount); p++) {
		t = md->paramtypes[p].type;
		if (IS_INT_LNG_TYPE(t)) {
			M_LLD(rd->argintregs[p], REG_SP,LA_SIZE + PA_SIZE + 8 + p * 8);
		} else { /* Float/Dbl */
			if (!md->params[p].inmemory) { /* Param in Arg Reg */
				/* use reserved Place on Stack (sp + 5 * 16) to copy  */
				/* float/double arg reg to int reg                    */
				s1 = rd->argfltregs[md->params[p].regoff];
				M_MOV(s1, rd->argintregs[p]);
			} else	{
				assert(0);
			}
		}
	}
#endif

	/* put methodinfo pointer on Stackframe */
	p = dseg_addaddress(cd, m);
	M_ALD(REG_ITMP1, REG_PV, p);
#if defined(__DARWIN__)
	M_AST(REG_ITMP1, REG_SP, LA_SIZE + TRACE_ARGS_NUM * 8); 
#else
	if (TRACE_ARGS_NUM == 8)	{
		/* need to pass via stack */
		M_AST(REG_ITMP1, REG_SP, LA_SIZE + PA_SIZE);
	} else {
		/* pass via register, reg 3 is the first  */
		M_MOV(REG_ITMP1, 3 + TRACE_ARGS_NUM);
	}
#endif
	/* call via function descriptor */
	/* XXX: what about TOC? */
	p = dseg_addaddress(cd, builtin_trace_args);
	M_ALD(REG_ITMP2, REG_PV, p);
	M_ALD(REG_ITMP1, REG_ITMP2, 0);
	M_MTCTR(REG_ITMP1);
	M_JSR;

#if defined(__DARWIN__)
	/* restore integer argument registers from the reserved stack space */

	stack_off = LA_SIZE;
	for (p = 0; p < md->paramcount && p < TRACE_ARGS_NUM; p++, stack_off += 8) {
		t = md->paramtypes[p].type;

		if (IS_INT_LNG_TYPE(t)) {
			if (!md->params[p].inmemory) {
				M_LLD(rd->argintregs[md->params[p].regoff], REG_SP, stack_off);
			} else  {
				assert(0);
			}
		}
	}
#else
	/* LINUX */
	for (p = 0; p < md->paramcount && p < TRACE_ARGS_NUM; p++) {
		d = rd->argintregs[p];
		/* restore integer argument registers */
		M_LLD(d, REG_SP, LA_SIZE + PA_SIZE + 8 + p * 8);
	}
#endif
	M_ALD(REG_ZERO, REG_SP, stack_size + LA_LR_OFFSET);
	M_MTLR(REG_ZERO);
	M_LDA(REG_SP, REG_SP, stack_size);
}

/* emit_verbosecall_exit ******************************************************
 *
 *    Generates the code for the call trace.
 *
 ********************************************************************************/
void emit_verbosecall_exit(jitdata *jd)
{
	codegendata *cd = jd->cd;
	s4 disp;

	M_MFLR(REG_ZERO);
	M_LDA(REG_SP, REG_SP, -(LA_SIZE+PA_SIZE+10*8));
	M_DST(REG_FRESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
	M_LST(REG_RESULT, REG_SP, LA_SIZE+PA_SIZE+1*8);
	M_AST(REG_ZERO, REG_SP, LA_SIZE+PA_SIZE+2*8);

#if defined(__DARWIN__)
	M_MOV(REG_RESULT, jd->rd->argintregs[1]);
#else
	M_MOV(REG_RESULT, jd->rd->argintregs[1]);
#endif

	disp = dseg_addaddress(cd, jd->m);
	M_ALD(jd->rd->argintregs[0], REG_PV, disp);

	M_FLTMOVE(REG_FRESULT, jd->rd->argfltregs[0]);
	M_FLTMOVE(REG_FRESULT, jd->rd->argfltregs[1]);
	disp = dseg_addaddress(cd, builtin_displaymethodstop);
	/* call via function descriptor, XXX: what about TOC ? */
	M_ALD(REG_ITMP2, REG_PV, disp);
	M_ALD(REG_ITMP2, REG_ITMP2, 0);
	M_MTCTR(REG_ITMP2);
	M_JSR;

	M_DLD(REG_FRESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
	M_LLD(REG_RESULT, REG_SP, LA_SIZE+PA_SIZE+1*8);
	M_ALD(REG_ZERO, REG_SP, LA_SIZE+PA_SIZE+2*8);
	M_LDA(REG_SP, REG_SP, LA_SIZE+PA_SIZE+10*8);
	M_MTLR(REG_ZERO);
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
