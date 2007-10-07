/* src/vm/jit/powerpc64/emit.c - PowerPC64 code emitter functions

   Copyright (C) 1996-2005, 2006, 2007 R. Grafl, A. Krall, C. Kruegel,
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

*/


#include "config.h"

#include <assert.h>

#include "vm/types.h"

#include "mm/memory.h"

#include "md-abi.h"
#include "vm/jit/powerpc64/codegen.h"

#include "threads/lock-common.h"

#include "vm/exceptions.h"
#include "vm/vm.h"

#include "vm/jit/abi.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/emit-common.h"
#include "vm/jit/jit.h"
#include "vm/jit/trace.h"

#include "vmcore/options.h"


/* emit_load *******************************************************************

   Emits a possible load of an operand.

*******************************************************************************/

s4 emit_load(jitdata *jd, instruction *iptr, varinfo *src, s4 tempreg)
{
	codegendata  *cd;
	s4            disp;
	s4            reg;

	/* get required compiler data */

	cd = jd->cd;

	if (src->flags & INMEMORY) {
		COUNT_SPILLS;

		disp = src->vv.regoff;

		if (IS_FLT_DBL_TYPE(src->type)) {
			M_DLD(tempreg, REG_SP, disp);
		}
		else {
			M_LLD(tempreg, REG_SP, disp);
		}

		reg = tempreg;
	}
	else
		reg = src->vv.regoff;

	return reg;
}


/* emit_store ******************************************************************

   Emits a possible store to a variable.

*******************************************************************************/

void emit_store(jitdata *jd, instruction *iptr, varinfo *dst, s4 d)
{
	codegendata  *cd;

	/* get required compiler data */

	cd = jd->cd;

	if (dst->flags & INMEMORY) {
		COUNT_SPILLS;

		if (IS_FLT_DBL_TYPE(dst->type)) {
			M_DST(d, REG_SP, dst->vv.regoff);
		}
		else {
			M_LST(d, REG_SP, dst->vv.regoff);
		}
	}
}


/* emit_copy *******************************************************************

   Generates a register/memory to register/memory copy.

*******************************************************************************/

void emit_copy(jitdata *jd, instruction *iptr)
{
	codegendata *cd;
	varinfo     *src;
	varinfo     *dst;
	s4           s1, d;

	/* get required compiler data */

	cd = jd->cd;

	/* get source and destination variables */

	src = VAROP(iptr->s1);
	dst = VAROP(iptr->dst);

	if ((src->vv.regoff != dst->vv.regoff) ||
		((src->flags ^ dst->flags) & INMEMORY)) {

		if ((src->type == TYPE_RET) || (dst->type == TYPE_RET)) {
			/* emit nothing, as the value won't be used anyway */
			return;
		}

		/* If one of the variables resides in memory, we can eliminate
		   the register move from/to the temporary register with the
		   order of getting the destination register and the load. */

		if (IS_INMEMORY(src->flags)) {
			d  = codegen_reg_of_var(iptr->opc, dst, REG_IFTMP);
			s1 = emit_load(jd, iptr, src, d);
		}
		else {
			s1 = emit_load(jd, iptr, src, REG_IFTMP);
			d  = codegen_reg_of_var(iptr->opc, dst, s1);
		}

		if (s1 != d) {
			if (IS_FLT_DBL_TYPE(src->type))
				M_FMOV(s1, d);
			else
				M_MOV(s1, d);
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

	if ((value >= -32768) && (value <= 32767)) {
		M_LDA_INTERN(d, REG_ZERO, value);
	} else {
		disp = dseg_add_s4(cd, value);
		M_ILD(d, REG_PV, disp);
	}
}

void emit_lconst(codegendata *cd, s4 d, s8 value)
{
	s4 disp;
	if ((value >= -32768) && (value <= 32767)) {
		M_LDA_INTERN(d, REG_ZERO, value);
	} else {
		disp = dseg_add_s8(cd, value);
		M_LLD(d, REG_PV, disp);
	}
}


/* emit_verbosecall_enter ******************************************************

   Generates the code for the call trace.

*******************************************************************************/

#if !defined(NDEBUG)
void emit_verbosecall_enter(jitdata *jd)
{
	methodinfo   *m;
	codegendata  *cd;
	methoddesc   *md;
	int32_t       paramcount;
	int32_t       stackframesize;
	s4            disp;
	s4            i, s;

	/* get required compiler data */

	m  = jd->m;
	cd = jd->cd;

	md = m->parseddesc;
	
	/* mark trace code */

	M_NOP;

	/* align stack to 16-bytes */

	paramcount = md->paramcount;
	ALIGN_2(paramcount);
	stackframesize = LA_SIZE + PA_SIZE + md->paramcount * 8;

	M_MFLR(REG_ZERO);
	M_AST(REG_ZERO, REG_SP, LA_LR_OFFSET);
	M_STDU(REG_SP, REG_SP, -stackframesize);

#if defined(__DARWIN__)
	#warning "emit_verbosecall_enter not implemented"
#else
	/* save argument registers */

	for (i = 0; i < md->paramcount; i++) {
		if (!md->params[i].inmemory) {
			s = md->params[i].regoff;

			switch (md->paramtypes[i].type) {
			case TYPE_ADR:
			case TYPE_INT:
			case TYPE_LNG:
				M_LST(s, REG_SP, LA_SIZE+PA_SIZE+i*8);
				break;
			case TYPE_FLT:
			case TYPE_DBL:
				M_DST(s, REG_SP, LA_SIZE+PA_SIZE+i*8);
				break;
			}
		}
	}
#endif

	disp = dseg_add_address(cd, m);
	M_ALD(REG_A0, REG_PV, disp);
	M_AADD_IMM(REG_SP, LA_SIZE+PA_SIZE, REG_A1);
	M_AADD_IMM(REG_SP, stackframesize + cd->stackframesize * 8, REG_A2);
	/* call via function descriptor, XXX: what about TOC? */
	disp = dseg_add_functionptr(cd, trace_java_call_enter);
	M_ALD(REG_ITMP2, REG_PV, disp);
	M_ALD(REG_ITMP1, REG_ITMP2, 0);
	M_MTCTR(REG_ITMP1);
	M_JSR;

#if defined(__DARWIN__)
	#warning "emit_verbosecall_enter not implemented"
#else
	/* restore argument registers */

	for (i = 0; i < md->paramcount; i++) {
		if (!md->params[i].inmemory) {
			s = md->params[i].regoff;

			switch (md->paramtypes[i].type) {
			case TYPE_ADR:
			case TYPE_INT:
			case TYPE_LNG:
				M_LLD(s, REG_SP, LA_SIZE+PA_SIZE+i*8);
				break;
			case TYPE_FLT:
			case TYPE_DBL:
				M_DLD(s, REG_SP, LA_SIZE+PA_SIZE+i*8);
				break;
			}
		}
	}
#endif

	M_ALD(REG_ZERO, REG_SP, stackframesize + LA_LR_OFFSET);
	M_MTLR(REG_ZERO);
	M_LDA(REG_SP, REG_SP, stackframesize);

	/* mark trace code */

	M_NOP;
}
#endif


/* emit_verbosecall_exit ******************************************************

   Generates the code for the call trace.

*******************************************************************************/

#if !defined(NDEBUG)
void emit_verbosecall_exit(jitdata *jd)
{
	methodinfo   *m;
	codegendata  *cd;
	methoddesc   *md;
	s4            disp;

	/* get required compiler data */

	m  = jd->m;
	cd = jd->cd;

	md = m->parseddesc;

	/* mark trace code */

	M_NOP;

	M_MFLR(REG_ZERO);
	M_LDA(REG_SP, REG_SP, -(LA_SIZE+PA_SIZE+10*8));
	M_AST(REG_ZERO, REG_SP, LA_SIZE+PA_SIZE+1*8);

	/* save return value */

	switch (md->returntype.type) {
	case TYPE_ADR:
	case TYPE_INT:
	case TYPE_LNG:
		M_LST(REG_RESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
		break;
	case TYPE_FLT:
	case TYPE_DBL:
		M_DST(REG_FRESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
		break;
	}

	disp = dseg_add_address(cd, m);
	M_ALD(REG_A0, REG_PV, disp);
	M_AADD_IMM(REG_SP, LA_SIZE+PA_SIZE, REG_A1);

	disp = dseg_add_functionptr(cd, trace_java_call_exit);
	/* call via function descriptor, XXX: what about TOC ? */
	M_ALD(REG_ITMP2, REG_PV, disp);
	M_ALD(REG_ITMP2, REG_ITMP2, 0);
	M_MTCTR(REG_ITMP2);
	M_JSR;

	/* restore return value */

	switch (md->returntype.type) {
	case TYPE_ADR:
	case TYPE_INT:
	case TYPE_LNG:
		M_LLD(REG_RESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
		break;
	case TYPE_FLT:
	case TYPE_DBL:
		M_DLD(REG_FRESULT, REG_SP, LA_SIZE+PA_SIZE+0*8);
		break;
	}

	M_ALD(REG_ZERO, REG_SP, LA_SIZE+PA_SIZE+1*8);
	M_LDA(REG_SP, REG_SP, LA_SIZE+PA_SIZE+10*8);
	M_MTLR(REG_ZERO);

	/* mark trace code */

	M_NOP;
}
#endif


/* emit_branch *****************************************************************

   Emits the code for conditional and unconditional branchs.

*******************************************************************************/

void emit_branch(codegendata *cd, s4 disp, s4 condition, s4 reg, u4 opt)
{
	s4 checkdisp;
	s4 branchdisp;

	/* calculate the different displacements */

	checkdisp  =  disp + 4;
	branchdisp = (disp - 4) >> 2;

	/* check which branch to generate */

	if (condition == BRANCH_UNCONDITIONAL) {
		/* check displacement for overflow */

		if ((checkdisp < (s4) 0xfe000000) || (checkdisp > (s4) 0x01fffffc)) {
			/* if the long-branches flag isn't set yet, do it */

			if (!CODEGENDATA_HAS_FLAG_LONGBRANCHES(cd)) {
				cd->flags |= (CODEGENDATA_FLAG_ERROR |
							  CODEGENDATA_FLAG_LONGBRANCHES);
			}

			vm_abort("emit_branch: emit unconditional long-branch code");
		}
		else {
			M_BR(branchdisp);
		}
	}
	else {
		/* and displacement for overflow */

		if ((checkdisp < (s4) 0xffff8000) || (checkdisp > (s4) 0x00007fff)) {
			/* if the long-branches flag isn't set yet, do it */

			if (!CODEGENDATA_HAS_FLAG_LONGBRANCHES(cd)) {
				cd->flags |= (CODEGENDATA_FLAG_ERROR |
							  CODEGENDATA_FLAG_LONGBRANCHES);
			}

			branchdisp --;		/* we jump from the second instruction */
			switch (condition) {
			case BRANCH_EQ:
				M_BNE(1);
				M_BR(branchdisp);
				break;
			case BRANCH_NE:
				M_BEQ(1);
				M_BR(branchdisp);
				break;
			case BRANCH_LT:
				M_BGE(1);
				M_BR(branchdisp);
				break;
			case BRANCH_GE:
				M_BLT(1);
				M_BR(branchdisp);
				break;
			case BRANCH_GT:
				M_BLE(1);
				M_BR(branchdisp);
				break;
			case BRANCH_LE:
				M_BGT(1);
				M_BR(branchdisp);
				break;
			case BRANCH_NAN:
				vm_abort("emit_branch: long BRANCH_NAN");
				break;
			default:
				vm_abort("emit_branch: unknown condition %d", condition);
			}

		}
		else {
			switch (condition) {
			case BRANCH_EQ:
				M_BEQ(branchdisp);
				break;
			case BRANCH_NE:
				M_BNE(branchdisp);
				break;
			case BRANCH_LT:
				M_BLT(branchdisp);
				break;
			case BRANCH_GE:
				M_BGE(branchdisp);
				break;
			case BRANCH_GT:
				M_BGT(branchdisp);
				break;
			case BRANCH_LE:
				M_BLE(branchdisp);
				break;
			case BRANCH_NAN:
				M_BNAN(branchdisp);
				break;
			default:
				vm_abort("emit_branch: unknown condition %d", condition);
			}
		}
	}
}

/* emit_arrayindexoutofbounds_check ********************************************

   Emit a ArrayIndexOutOfBoundsException check.

*******************************************************************************/

void emit_arrayindexoutofbounds_check(codegendata *cd, instruction *iptr, s4 s1, s4 s2)
{
	if (checkbounds) {
#define SOFTEX 0
#if SOFTEX
		M_ILD(REG_ITMP3, s1, OFFSET(java_array_t, size));
		M_CMPU(s2, REG_ITMP3);
		codegen_add_arrayindexoutofboundsexception_ref(cd, s2);
		BRANCH_NOPS;
#else
		M_ILD(REG_ITMP3, s1, OFFSET(java_array_t, size));
		M_CMPU(s2, REG_ITMP3);
		M_BLT(1);
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(s2, REG_ZERO, EXCEPTION_HARDWARE_ARRAYINDEXOUTOFBOUNDS);
#endif
	}
}


/* emit_arraystore_check *******************************************************

   Emit an ArrayStoreException check.

*******************************************************************************/

void emit_arraystore_check(codegendata *cd, instruction *iptr)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
		M_TST(REG_RESULT);
		M_BNE(1);
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(REG_ZERO, REG_ZERO, EXCEPTION_HARDWARE_ARRAYSTORE);
	}
}


/* emit_arithmetic_check *******************************************************

   Emit an ArithmeticException check.

*******************************************************************************/

void emit_arithmetic_check(codegendata *cd, instruction *iptr, s4 reg)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
	#if SOFTEX
		M_TST(reg);
		codegen_add_arithmeticexception_ref(cd);
		BRANCH_NOPS;
	#else
		M_TST(reg);
		M_BNE(1);
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(REG_ZERO, REG_ZERO, EXCEPTION_HARDWARE_ARITHMETIC);
	#endif
	}
}

#if 0
/* emit_arraystore_check *******************************************************

   Emit an ArrayStoreException check.

*******************************************************************************/

void emit_arraystore_check(codegendata *cd, instruction *iptr, s4 reg)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
		M_TST(REG_RESULT);
		codegen_add_arraystoreexception_ref(cd);
		BRANCH_NOPS;
	}
}
#endif

/* emit_classcast_check ********************************************************

   Emit a ClassCastException check.

*******************************************************************************/

void emit_classcast_check(codegendata *cd, instruction *iptr, s4 condition, s4 reg, s4 s1)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
	#if SOFTEX
		codegen_add_classcastexception_ref(cd, condition, s1);
		BRANCH_NOPS;
		M_NOP;
	#else
		switch(condition)	{
			case BRANCH_LE:
				M_BGT(1);
				break;
			case BRANCH_EQ:
				M_BNE(1);
				break;
			case BRANCH_GT:
				M_BLE(1);
				break;
			default:
				vm_abort("emit_classcast_check: unknown condition %d", condition);
		}
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(s1, REG_ZERO, EXCEPTION_HARDWARE_CLASSCAST);
	#endif
	}
}


/* emit_nullpointer_check ******************************************************

   Emit a NullPointerException check.

*******************************************************************************/

void emit_nullpointer_check(codegendata *cd, instruction *iptr, s4 reg)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
		M_TST(reg);
		M_BNE(1);
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(REG_ZERO, REG_ZERO, EXCEPTION_HARDWARE_NULLPOINTER);
	}
}

/* emit_exception_check ********************************************************

   Emit an Exception check.

*******************************************************************************/

void emit_exception_check(codegendata *cd, instruction *iptr)
{
	if (INSTRUCTION_MUST_CHECK(iptr))	{
	#if SOFTEX
		M_CMPI(REG_RESULT, 0);
		codegen_add_fillinstacktrace_ref(cd);
		BRANCH_NOPS;
	#else
		M_TST(REG_RESULT);
		M_BNE(1);
		/* ALD is 4 byte aligned, ILD 2, onyl LWZ is byte aligned */
		M_LWZ(REG_ZERO, REG_ZERO, EXCEPTION_HARDWARE_EXCEPTION);
	#endif
	}
}


/* emit_trap_compiler **********************************************************

   Emit a trap instruction which calls the JIT compiler.

*******************************************************************************/

void emit_trap_compiler(codegendata *cd)
{
	M_LWZ(REG_METHODPTR, REG_ZERO, EXCEPTION_HARDWARE_COMPILER);
}


/* emit_trap *******************************************************************

   Emit a trap instruction and return the original machine code.

*******************************************************************************/

uint32_t emit_trap(codegendata *cd)
{
	uint32_t mcode;

	/* Get machine code which is patched back in later. The
	   trap is 1 instruction word long. */

	mcode = *((uint32_t *) cd->mcodeptr);

	/* ALD is 4 byte aligned, ILD 2, only LWZ is byte aligned */
	M_LWZ(REG_ZERO, REG_ZERO, EXCEPTION_HARDWARE_PATCHER);

	return mcode;
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
