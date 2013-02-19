/* src/vm/jit/m68k/codegen.c - machine code generator for m68k

   Copyright (C) 1996-2005, 2006, 2007, 2008, 2009
   CACAOVM - Verein zur Foerderung der freien virtuellen Maschine CACAO

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
#include <stdint.h>

#include "md-abi.h"

#include "vm/types.h"
#include "vm/jit/m68k/codegen.h"
#include "vm/jit/m68k/emit.h"

#include "mm/memory.hpp"

#include "native/localref.hpp"
#include "native/native.hpp"

#include "threads/lock.hpp"

#include "vm/jit/builtin.hpp"
#include "vm/exceptions.hpp"
#include "vm/global.h"
#include "vm/loader.hpp"
#include "vm/options.h"
#include "vm/utf8.hpp"
#include "vm/vm.hpp"

#include "vm/jit/asmpart.h"
#include "vm/jit/codegen-common.hpp"
#include "vm/jit/patcher-common.hpp"
#include "vm/jit/dseg.h"
#include "vm/jit/linenumbertable.hpp"
#include "vm/jit/emit-common.hpp"
#include "vm/jit/jit.hpp"
#include "vm/jit/abi.h"
#include "vm/jit/parse.hpp"
#include "vm/jit/reg.h"
#include "vm/jit/stacktrace.hpp"
#include "vm/jit/trap.hpp"


/**
 * Generates machine code for the method prolog.
 */
void codegen_emit_prolog(jitdata* jd)
{
	varinfo*    var;
	methoddesc* md;
	int32_t     s1;
	int32_t     p, t, l;
	int32_t     varindex;
	int         i;

	// Get required compiler data.
	methodinfo*   m    = jd->m;
	codeinfo*     code = jd->code;
	codegendata*  cd   = jd->cd;
	registerdata* rd   = jd->rd;

	// XXX XXX
	// XXX Fix the below stuff, cd->stackframesize is a slot counter
	//     and not a byte counter!!!
	// XXX XXX

	/* create stack frame */
	M_AADD_IMM(-(cd->stackframesize), REG_SP);

	/* save used callee saved registers */
	p = cd->stackframesize;
	for (i=INT_SAV_CNT-1; i>=rd->savintreguse; --i)	{
		p-=8; M_IST(rd->savintregs[i], REG_SP, p);
	}
	for (i=ADR_SAV_CNT-1; i>=rd->savadrreguse; --i)	{
		p-=8; M_AST(rd->savadrregs[i], REG_SP, p);
	}
#if !defined(ENABLE_SOFTFLOAT)
	for (i=FLT_SAV_CNT-1; i>=rd->savfltreguse; --i)	{
		p-=8; M_FSTORE(rd->savfltregs[i], REG_SP, p);
	}	
#else
	assert(FLT_SAV_CNT == 0);
	assert(rd->savfltreguse == 0);
#endif
	/* take arguments out of stack frame */
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
		assert(md->params[p].inmemory);			/* all args are on stack */

		switch (t)	{
#if defined(ENABLE_SOFTFLOAT)
		case TYPE_FLT:
		case TYPE_DBL:
#endif
		case TYPE_LNG:
		case TYPE_INT:
			if (!IS_INMEMORY(var->flags)) {      /* stack arg -> register */
				if (IS_2_WORD_TYPE(t))	{
					M_LLD(var->vv.regoff, REG_SP, cd->stackframesize + s1 + 4);
				} else {
					M_ILD(var->vv.regoff, REG_SP, cd->stackframesize + s1 + 4);
				}
			} else {                             /* stack arg -> spilled  */
					M_ILD(REG_ITMP1, REG_SP, cd->stackframesize + s1 + 4);
					M_IST(REG_ITMP1, REG_SP, var->vv.regoff);
				if (IS_2_WORD_TYPE(t)) {
					M_ILD(REG_ITMP1, REG_SP, cd->stackframesize  + s1 + 4 + 4);
					M_IST(REG_ITMP1, REG_SP, var->vv.regoff + 4);
				}
			} 
			break;
#if !defined(ENABLE_SOFTFLOAT)
		case TYPE_FLT:
		case TYPE_DBL:
				if (!IS_INMEMORY(var->flags)) {      /* stack-arg -> register */
				if (IS_2_WORD_TYPE(t))	{
					M_DLD(var->vv.regoff, REG_SP, cd->stackframesize + s1 + 4);
				} else {
					M_FLD(var->vv.regoff, REG_SP, cd->stackframesize + s1 + 4);
				}
				} else {                             /* stack-arg -> spilled  */
				if (IS_2_WORD_TYPE(t)) {
					M_DLD(REG_FTMP1, REG_SP, cd->stackframesize + s1 + 4);
					M_DST(REG_FTMP1, REG_SP, var->vv.regoff);
				} else {
					M_FLD(REG_FTMP1, REG_SP, cd->stackframesize + s1 + 4);
					M_FST(REG_FTMP1, REG_SP, var->vv.regoff);
				}
			}
			break;
#endif /* SOFTFLOAT */
		case TYPE_ADR:
				if (!IS_INMEMORY(var->flags)) {      /* stack-arg -> register */
				M_ALD(var->vv.regoff, REG_SP, cd->stackframesize + s1 + 4);
				} else {                             /* stack-arg -> spilled  */
				M_ALD(REG_ATMP1, REG_SP, cd->stackframesize + s1 + 4);
				M_AST(REG_ATMP1, REG_SP, var->vv.regoff);
			}
			break;
		default: assert(0);
		}
	} /* end for argument out of stack*/
}


/**
 * Generates machine code for the method epilog.
 */
void codegen_emit_epilog(jitdata* jd)
{
	int32_t p;
	int i;

	// Get required compiler data.
	codeinfo*     code = jd->code;
	codegendata*  cd   = jd->cd;
	registerdata* rd   = jd->rd;

	p = cd->stackframesize;

	/* restore return address */

#if 0
	if (!code_is_leafmethod(code)) {
		/* ATTENTION: Don't use REG_ZERO (r0) here, as M_ALD
		   may have a displacement overflow. */

		M_ALD(REG_ITMP1, REG_SP, p * 4 + LA_LR_OFFSET);
		M_MTLR(REG_ITMP1);
	}
#endif

	/* restore saved registers */

	for (i = INT_SAV_CNT - 1; i >= rd->savintreguse; i--) {
		p-=8; M_ILD(rd->savintregs[i], REG_SP, p);
	}
	for (i = ADR_SAV_CNT - 1; i >= rd->savadrreguse; --i) {
		p-=8; M_ALD(rd->savadrregs[i], REG_SP, p);
	}
#if !defined(ENABLE_SOFTFLOAT)
	for (i = FLT_SAV_CNT - 1; i >= rd->savfltreguse; i--) {
		p-=8; M_FLOAD(rd->savfltregs[i], REG_SP, p);
	}
#endif

	/* deallocate stack */
	M_AADD_IMM(cd->stackframesize, REG_SP);
	M_RET;
}


/**
 * Generates machine code for one ICMD.
 */
void codegen_emit_instruction(jitdata* jd, instruction* iptr)
{
	varinfo*            var;
	builtintable_entry* bte;
	methodinfo*         lm;             // Local methodinfo for ICMD_INVOKE*.
	unresolved_method*  um;
	fieldinfo*          fi;
	unresolved_field*   uf;
	int32_t             fieldtype;
	int32_t             s1, s2, s3, d;
	int32_t             disp;

	// Get required compiler data.
	codeinfo*     code = jd->code;
	codegendata*  cd   = jd->cd;

	switch (iptr->opc) {

		/* CONST **************************************************************/

		case ICMD_FCONST:     /* ...  ==> ..., constant                       */

#if defined(ENABLE_SOFTFLOAT)
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_IMOV_IMM(iptr->sx.val.i, d);
			emit_store_dst(jd, iptr, d);
#else
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			FCONST(iptr->sx.val.i, d);
			emit_store_dst(jd, iptr, d);
#endif
			break;

		case ICMD_DCONST:     /* ...  ==> ..., constant                       */

#if defined(ENABLE_SOFTFLOAT)
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			LCONST(iptr->sx.val.l, d);
			emit_store_dst(jd, iptr, d);
#else
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			disp = dseg_add_double(cd, iptr->sx.val.d);
			M_AMOV_IMM(0, REG_ATMP1);
			dseg_adddata(cd);
			M_DLD(d, REG_ATMP1, disp);
			emit_store_dst(jd, iptr, d);
#endif
			break;


		/* some long operations *********************************************/
		case ICMD_LADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP3);
			s2 = emit_load_s2_low(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			M_INTMOVE(s2, REG_ITMP1);
			M_IADD(s1, REG_ITMP1);			/* low word */
			s1 = emit_load_s1_high(jd, iptr, REG_ITMP3);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IADDX(s1, REG_ITMP2);			/* high word */
			emit_store_dst(jd, iptr, d);
			break;
			
		case ICMD_LADDCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.l = constant                          */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s1_high(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			
			M_IMOV_IMM(iptr->sx.val.l >> 32, REG_ITMP3);

			s3 = iptr->sx.val.l & 0xffffffff;
			M_INTMOVE(s1, REG_ITMP1);
			M_IADD_IMM(s3, REG_ITMP1);		/* lower word in REG_ITMP1 now */

			M_IADDX(REG_ITMP3, REG_ITMP2);	/* high word in REG_ITMP2 now */
			M_LNGMOVE(REG_ITMP12_PACKED, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2_low(jd, iptr, REG_ITMP3);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			M_INTMOVE(s1, REG_ITMP1);
			M_ISUB(s2, REG_ITMP1);			/* low word */
			s1 = emit_load_s1_high(jd, iptr, REG_ITMP2);
			s2 = emit_load_s2_high(jd, iptr, REG_ITMP3);
			M_INTMOVE(s1, REG_ITMP2);
			M_ISUBX(s2, REG_ITMP2);			/* high word */
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LSUBCONST:  /* ..., value  ==> ..., value - constant        */
		                      /* sx.val.l = constant                          */
			s1 = emit_load_s1_low(jd, iptr, REG_ITMP1);
			s2 = emit_load_s1_high(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			
			M_IMOV_IMM( (-iptr->sx.val.l) >> 32, REG_ITMP3);

			s3 = (-iptr->sx.val.l) & 0xffffffff;
			M_INTMOVE(s1, REG_ITMP1);
			M_IADD_IMM(s3, REG_ITMP1);		/* lower word in REG_ITMP1 now */

			M_IADDX(REG_ITMP3, REG_ITMP2);	/* high word in REG_ITMP2 now */
			M_LNGMOVE(REG_ITMP12_PACKED, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LNEG:       /* ..., value  ==> ..., - value                 */
			s1 = emit_load_s1(jd, iptr, REG_ITMP12_PACKED);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			M_LNGMOVE(s1, REG_ITMP12_PACKED);
			M_INEG(GET_LOW_REG(REG_ITMP12_PACKED));
			M_INEGX(GET_HIGH_REG(REG_ITMP12_PACKED));
			M_LNGMOVE(REG_ITMP12_PACKED, d);
			emit_store_dst(jd, iptr, d);
			break;

		/* integer operations ************************************************/
		case ICMD_INEG:       /* ..., value  ==> ..., - value                 */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1); 
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_INEG(REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_I2L:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1(jd, iptr, REG_ITMP3);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			M_IMOV(s1, GET_LOW_REG(d));				/* sets negativ bit */
			M_BPL(4);
			M_ISET(GET_HIGH_REG(d));
			M_TPFW;
			M_ICLR(GET_HIGH_REG(d));

			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_L2I:        /* ..., value  ==> ..., value                   */

			s1 = emit_load_s1_low(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_INTMOVE(s1, d);
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
			M_INTMOVE(s2, REG_ITMP2);
			M_IADD(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		                      /* s1.localindex = variable, sx.val.i = constant*/

		case ICMD_IINC:
		case ICMD_IADDCONST:

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_IADD_IMM(iptr->sx.val.i, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISUB:       /* ..., val1, val2  ==> ..., val1 - val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_ISUB(s2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISUBCONST:  /* ..., value  ==> ..., value + constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_IADD_IMM(-iptr->sx.val.i, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			emit_arithmetic_check(cd, iptr, s2);
			M_INTMOVE(s1, REG_ITMP1);
			M_IDIV(s2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IDIVPOW2:		/* ..., value  ==> ..., value << constant       */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);

			M_ITST(REG_ITMP1);
			M_BPL(6);
			M_IADD_IMM((1 << iptr->sx.val.i) - 1, REG_ITMP1);

			M_IMOV_IMM(iptr->sx.val.i, REG_ITMP2);
			M_ISSR(REG_ITMP2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IREM:       /* ..., val1, val2  ==> ..., val1 % val2        */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP3);
			emit_arithmetic_check(cd, iptr, s2);

			M_ICMP_IMM(0x80000000, s1);
			M_BNE(4+8);
			M_ICMP_IMM(-1, s2);
			M_BNE(4);
			M_ICLR(REG_ITMP3);
			M_TPFL;					/* hides the next instruction */
			M_IREM(s2, s1, REG_ITMP3);

			M_INTMOVE(REG_ITMP3, d);

			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IREMPOW2:		/* ..., value  ==> ..., value << constant       */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			if (s1 == d) {
				M_IMOV(s1, REG_ITMP1);
				s1 = REG_ITMP1;
			} 
			M_INTMOVE(s1, d);
			M_IAND_IMM(iptr->sx.val.i, d);
			M_ITST(s1);
			M_BGE(2 + 2 + 6 + 2);
			M_IMOV(s1, d);  /* don't use M_INTMOVE, so we know the jump offset */
			M_INEG(d);
			M_IAND_IMM(iptr->sx.val.i, d);     /* use 32-bit for jump offset */
			M_INEG(d);

			emit_store_dst(jd, iptr, d);
			break;


		case ICMD_LDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
		case ICMD_LREM:       /* ..., val1, val2  ==> ..., val1 % val2        */

			bte = iptr->sx.s23.s3.bte;
			md  = bte->md;

			s2 = emit_load_s2(jd, iptr, REG_ITMP12_PACKED);
			M_INTMOVE(GET_LOW_REG(s2), REG_ITMP3);
			M_IOR(GET_HIGH_REG(s2), REG_ITMP3);
			/* XXX could be optimized */
			emit_arithmetic_check(cd, iptr, REG_ITMP3);

			M_LST(s2, REG_SP, 2 * 4);
			s1 = emit_load_s1(jd, iptr, REG_ITMP12_PACKED);
			M_LST(s1, REG_SP, 0 * 4);

			M_JSR_IMM(bte->fp);

			d = codegen_reg_of_dst(jd, iptr, REG_RESULT_PACKED);
			M_LNGMOVE(REG_RESULT_PACKED, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IMUL(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IMULCONST:  /* ..., value  ==> ..., value * constant        */
		                      /* sx.val.i = constant                          */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_IMOV_IMM(iptr->sx.val.i, REG_ITMP2);
			M_IMUL(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHL:       /* ..., val1, val2  ==> ..., val1 << val2       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_INTMOVE(s2, REG_ITMP2);
			M_IAND_IMM(0x1f, REG_ITMP2);
			M_ISSL(REG_ITMP2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHLCONST:  /* ..., value  ==> ..., value << constant       */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			if (iptr->sx.val.i & 0x1f)	{
				M_INTMOVE(s1, REG_ITMP1)
				if ((iptr->sx.val.i & 0x1f) <= 7)	{
					M_ISSL_IMM(iptr->sx.val.i & 0x1f, REG_ITMP1);
				} else	{
					M_IMOV_IMM(iptr->sx.val.i & 0x1f, REG_ITMP2);
					M_ISSL(REG_ITMP2, REG_ITMP1);
				}
				M_INTMOVE(REG_ITMP1, d);
			} else	{
				M_INTMOVE(s1, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHR:       /* ..., val1, val2  ==> ..., val1 >> val2       */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_INTMOVE(s2, REG_ITMP2);
			M_IAND_IMM(0x1f, REG_ITMP2);
			M_ISSR(REG_ITMP2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_ISHRCONST:  /* ..., value  ==> ..., value >> constant       */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			if (iptr->sx.val.i & 0x1f)	{
				M_INTMOVE(s1, REG_ITMP1)
				if ((iptr->sx.val.i & 0x1f) <= 7)	{
					M_ISSR_IMM(iptr->sx.val.i & 0x1f, REG_ITMP1);
				} else	{
					M_IMOV_IMM(iptr->sx.val.i & 0x1f, REG_ITMP2);
					M_ISSR(REG_ITMP2, REG_ITMP1);
				}
				M_INTMOVE(REG_ITMP1, d);
			} else	{
				M_INTMOVE(s1, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IUSHR:      /* ..., val1, val2  ==> ..., val1 >>> val2      */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_INTMOVE(s2, REG_ITMP2);
			M_IAND_IMM(0x1f, REG_ITMP2);
			M_IUSR(REG_ITMP2, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IUSHRCONST: /* ..., value  ==> ..., value >>> constant      */
		                      /* sx.val.i = constant                          */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			if (iptr->sx.val.i & 0x1f)	{
				M_INTMOVE(s1, REG_ITMP1)
				if ((iptr->sx.val.i & 0x1f) <= 7)	{
					M_IUSR_IMM(iptr->sx.val.i & 0x1f, REG_ITMP1);
				} else	{
					M_IMOV_IMM(iptr->sx.val.i & 0x1f, REG_ITMP2);
					M_IUSR(REG_ITMP2, REG_ITMP1);
				}
				M_INTMOVE(REG_ITMP1, d);
			} else	{
				M_INTMOVE(s1, d);
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IAND:       /* ..., val1, val2  ==> ..., val1 & val2        */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IAND(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IANDCONST:  /* ..., value  ==> ..., value & constant        */
		                      /* sx.val.i = constant                          */

			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_IAND_IMM(iptr->sx.val.i, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IOR:        /* ..., val1, val2  ==> ..., val1 | val2        */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IOR(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.i = constant                          */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_IOR_IMM(iptr->sx.val.i, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IXOR:        /* ..., val1, val2  ==> ..., val1 | val2        */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IXOR(s1, REG_ITMP2);
			M_INTMOVE(REG_ITMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IXORCONST:   /* ..., value  ==> ..., value | constant        */
		                      /* sx.val.i = constant                          */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			M_IXOR_IMM(iptr->sx.val.i, REG_ITMP1);
			M_INTMOVE(REG_ITMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		/* floating point operations ******************************************/
		#if !defined(ENABLE_SOFTFLOAT)
		case ICMD_FCMPL:		/* ..., val1, val2  ==> ..., val1 fcmpl val2  */
		case ICMD_DCMPL:
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_IMOV_IMM(-1, d);
			M_FCMP(s1, s2);
			M_BFUN(14);	/* result is -1, branch to end */
			M_BFLT(10);	/* result is -1, branch to end */
			M_IMOV_IMM(0, d);
			M_BFEQ(4)	/* result is 0, branch to end */
			M_IMOV_IMM(1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FCMPG:		/* ..., val1, val2  ==> ..., val1 fcmpg val2  */
		case ICMD_DCMPG:
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_IMOV_IMM(1, d);
			M_FCMP(s1, s2);
			M_BFUN(16);	/* result is +1, branch to end */
			M_BFGT(14);	/* result is +1, branch to end */
			M_IMOV_IMM(0, d);
			M_BFEQ(8)	/* result is 0, branch to end */
			M_IMOV_IMM(-1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_fmove(cd, s2, REG_FTMP2);
			M_FMUL(s1, REG_FTMP2);
			emit_fmove(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DMUL:       /* ..., val1, val2  ==> ..., val1 * val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_dmove(cd, s2, REG_FTMP2);
			M_DMUL(s1, REG_FTMP2);
			emit_dmove(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			emit_fmove(cd, s1, REG_FTMP1);
			M_FDIV(s2, REG_FTMP1);
			emit_fmove(cd, REG_FTMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DDIV:       /* ..., val1, val2  ==> ..., val1 / val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			emit_dmove(cd, s1, REG_FTMP1);
			M_DDIV(s2, REG_FTMP1);
			emit_dmove(cd, REG_FTMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_fmove(cd, s2, REG_FTMP2);
			M_FADD(s1, REG_FTMP2);
			emit_fmove(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DADD:       /* ..., val1, val2  ==> ..., val1 + val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_dmove(cd, s2, REG_FTMP2);
			M_DADD(s1, REG_FTMP2);
			emit_dmove(cd, REG_FTMP2, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_fmove(cd, s1, REG_FTMP1);
			M_FSUB(s2, REG_FTMP1);
			emit_fmove(cd, REG_FTMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DSUB:       /* ..., val1, val2  ==> ..., val1 - val2        */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			emit_dmove(cd, s1, REG_FTMP1);
			M_DSUB(s2, REG_FTMP1);
			emit_dmove(cd, REG_FTMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_F2D:       /* ..., value  ==> ..., (double) value           */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			M_F2D(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_D2F:       /* ..., value  ==> ..., (float) value           */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			M_D2F(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FNEG:       /* ..., value  ==> ..., - value                 */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			M_FNEG(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DNEG:       /* ..., value  ==> ..., - value                 */
			s1 = emit_load_s1(jd, iptr, REG_FTMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP2);
			M_DNEG(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		#endif


		case ICMD_ACONST:     /* ...  ==> ..., constant                       */
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				constant_classref *cr = iptr->sx.val.c.ref;;
				patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_classinfo, cr, 0);
				M_AMOV_IMM(0, d);
			} else {
				M_AMOV_IMM(iptr->sx.val.anyptr, d);
			}
			emit_store_dst(jd, iptr, d);
			break;
		/* BRANCH *************************************************************/

		case ICMD_ATHROW:       /* ..., objectref ==> ... (, objectref)       */

			M_JSR_PCREL(2);				/* get current PC */
			M_APOP(REG_ATMP2);		

			M_AMOV_IMM(asm_handle_exception, REG_ATMP3);
			M_JMP(REG_ATMP3);
			break;

		/* MEMORY *************************************************************/

		case ICMD_GETSTATIC:  /* ...  ==> ..., value                          */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;

				patcher_add_patch_ref(jd, PATCHER_get_putstatic, uf, 0);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = (intptr_t) fi->value;

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->clazz))	{
					patcher_add_patch_ref(jd, PATCHER_initialize_class, fi->clazz,
										0);
				}
			}

			M_AMOV_IMM(disp, REG_ATMP1);
			switch (fieldtype) {
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
#endif
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
				M_ILD(d, REG_ATMP1, 0);
				break;
			case TYPE_ADR:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
				M_ALD(d, REG_ATMP1, 0);
				break;
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_DBL:
#endif
			case TYPE_LNG:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP23_PACKED);
				M_LLD(d, REG_ATMP1, 0);
				break;
#if !defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_FLD(d, REG_ATMP1, 0);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_DLD(d, REG_ATMP1, 0);
				break;
#endif
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_PUTSTATIC:  /* ..., value  ==> ...                          */

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;

				patcher_add_patch_ref(jd, PATCHER_get_putstatic, uf, 0);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = (intptr_t) fi->value;

				if (!CLASS_IS_OR_ALMOST_INITIALIZED(fi->clazz))
					patcher_add_patch_ref(jd, PATCHER_initialize_class, fi->clazz,
										0);
  			}
		
			M_AMOV_IMM(disp, REG_ATMP1);
			switch (fieldtype) {
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
#endif
			case TYPE_INT:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_IST(s1, REG_ATMP1, 0);
				break;
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_DBL:
#endif
			case TYPE_LNG:
				s1 = emit_load_s1(jd, iptr, REG_ITMP23_PACKED);
				M_LST(s1, REG_ATMP1, 0);
				break;
			case TYPE_ADR:
				s1 = emit_load_s1(jd, iptr, REG_ITMP2);
				M_AST(s1, REG_ATMP1, 0);
				break;
#if !defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_FST(s1, REG_ATMP1, 0);
				break;
			case TYPE_DBL:
				s1 = emit_load_s1(jd, iptr, REG_FTMP2);
				M_DST(s1, REG_ATMP1, 0);
				break;
#endif
			default: assert(0);
			}
			break;

		case ICMD_GETFIELD:   /* ...  ==> ..., value                          */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				uf        = iptr->sx.s23.s3.uf;
				fieldtype = uf->fieldref->parseddesc.fd->type;
				disp      = 0;

				patcher_add_patch_ref(jd, PATCHER_get_putfield, uf, 0);
			}
			else {
				fi        = iptr->sx.s23.s3.fmiref->p.field;
				fieldtype = fi->type;
				disp      = fi->offset;
			}

			/* implicit null-pointer check */
			switch (fieldtype) {
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
#endif
			case TYPE_INT:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ILD(d, s1, disp);
				break;
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_DBL:
#endif
			case TYPE_LNG:
   				d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
				M_LLD(d, s1, disp);
				break;
			case TYPE_ADR:
				d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
				M_ALD(d, s1, disp);
				break;
#if !defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_FLD(d, s1, disp);
				break;
			case TYPE_DBL:				
				d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
				M_DLD(d, s1, disp);
				break;
#endif
			}
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_PUTFIELD:   /* ..., value  ==> ...                          */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);

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
				if (IS_2_WORD_TYPE(fieldtype)) {
					s2 = emit_load_s2(jd, iptr, REG_ITMP23_PACKED);
				} else {
					s2 = emit_load_s2(jd, iptr, REG_ITMP2);
				}
			} else {
				s2 = emit_load_s2(jd, iptr, REG_FTMP2);
			}

			if (INSTRUCTION_IS_UNRESOLVED(iptr))
				patcher_add_patch_ref(jd, PATCHER_get_putfield, uf, 0);

			/* implicit null-pointer check */
			switch (fieldtype) {
#if defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
#endif
			case TYPE_INT:
				M_IST(s2, s1, disp);
				break;

#if defined(ENABLE_SOFTFLOAT)
			case TYPE_DBL:
#endif
			case TYPE_LNG:
				M_LST(s2, s1, disp);  
				break;
			case TYPE_ADR:
				M_AST(s2, s1, disp);
				break;
#if !defined(ENABLE_SOFTFLOAT)
			case TYPE_FLT:
				M_FST(s2, s1, disp);
				break;
			case TYPE_DBL:
				M_DST(s2, s1, disp);
				break;
#endif
			}
			break;

		case ICMD_BALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_bytearray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_LBZX(REG_ATMP1, d);
			M_BSEXT(d, d);
			emit_store_dst(jd, iptr, d);
			break;			

		case ICMD_CALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(1, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_chararray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_LHZX(REG_ATMP1, d);
			M_CZEXT(d, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_SALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(1, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_shortarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
		
			/* implicit null-pointer check */
			M_LHZX(REG_ATMP1, d);
			M_SSEXT(d, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_IALOAD:     /* ..., arrayref, index  ==> ..., value         */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_intarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_LWZX(REG_ATMP1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_LALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			/* implicit null-pointer check */
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP1);
			M_ISSL_IMM(3, REG_ITMP1);
			M_IADD_IMM(OFFSET(java_longarray_t, data[0]), REG_ITMP1);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP1, REG_ATMP1);
			/* implicit null-pointer check */
			M_LLD(d, REG_ATMP1, 0);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_FALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_floatarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
#if !defined(ENABLE_SOFTFLOAT)
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_FLD(d, REG_ATMP1, 0);
#else
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP1);
			M_LWZX(REG_ATMP1, d);
#endif
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_DALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(3, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_doublearray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
#if !defined(ENABLE_SOFTFLOAT)
			d = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
			M_DLD(d, REG_ATMP1, 0);
#else
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP12_PACKED);
			M_LLD(d, REG_ATMP1, 0);
#endif
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_AALOAD:     /* ..., arrayref, index  ==> ..., value         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_objectarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
	
			/* implicit null-pointer check */
			M_LAX(REG_ATMP1, d);
			emit_store_dst(jd, iptr, d);
			break;


		case ICMD_BASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_INTMOVE(s2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_bytearray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_STBX(REG_ATMP1, s3);
			break;

		case ICMD_CASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(1, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_chararray_t, data[0]), REG_ITMP2); 
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_STHX(REG_ATMP1, s3);
			break;

		case ICMD_SASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(1, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_shortarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_STHX(REG_ATMP1, s3);
			break;

		case ICMD_IASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_intarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
			M_STWX(REG_ATMP1, s3);
			break;

		case ICMD_LASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP1);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);

			M_INTMOVE(s2, REG_ITMP1);
			M_ISSL_IMM(3, REG_ITMP1);
			M_IADD_IMM(OFFSET(java_longarray_t, data[0]), REG_ITMP1);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP1, REG_ATMP1);
			/* implicit null-pointer check */
			s3 = emit_load_s3(jd, iptr, REG_ITMP12_PACKED);
			M_LST(s3, REG_ATMP1, 0);
			break;

		case ICMD_FASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(2, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_floatarray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
#if !defined(ENABLE_SOFTFLOAT)
			s3 = emit_load_s3(jd, iptr, REG_FTMP3);
			M_FST(s3, REG_ATMP1, 0);
#else
			s3 = emit_load_s3(jd, iptr, REG_ITMP3);
			M_STWX(REG_ATMP1, s3);
#endif
			break;

		case ICMD_DASTORE:    /* ..., arrayref, index, value  ==> ...         */
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP2);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			M_INTMOVE(s2, REG_ITMP2);
			M_ISSL_IMM(3, REG_ITMP2);
			M_IADD_IMM(OFFSET(java_doublearray_t, data[0]), REG_ITMP2);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP2, REG_ATMP1);
			/* implicit null-pointer check */
#if !defined(ENABLE_SOFTFLOAT)
			s3 = emit_load_s3(jd, iptr, REG_FTMP3);
			M_DST(s3, REG_ATMP1, 0);
#else
			s3 = emit_load_s3(jd, iptr, REG_ITMP12_PACKED);
			/* implicit null-pointer check */
			M_LST(s3, REG_ATMP1, 0);
#endif
			break;

		case ICMD_AASTORE:    /* ..., arrayref, index, value  ==> ...         */

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP1);
			emit_arrayindexoutofbounds_check(cd, iptr, s1, s2);
			s3 = emit_load_s3(jd, iptr, REG_ATMP2);

			/* XXX what if array is NULL */
			disp = dseg_add_functionptr(cd, BUILTIN_FAST_canstore);

			M_AST(s1, REG_SP, 0*4);
			M_AST(s3, REG_SP, 1*4);
			M_JSR_IMM(BUILTIN_FAST_canstore);
			emit_arraystore_check(cd, iptr);

			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			s2 = emit_load_s2(jd, iptr, REG_ITMP1);
			s3 = emit_load_s3(jd, iptr, REG_ATMP2);
			M_INTMOVE(s2, REG_ITMP1);
			M_ISSL_IMM(2, REG_ITMP1);
			M_IADD_IMM(OFFSET(java_objectarray_t, data[0]), REG_ITMP1);
			M_ADRMOVE(s1, REG_ATMP1);
			M_AADDINT(REG_ITMP1, REG_ATMP1);
			/* implicit null-pointer check */
			M_STAX(REG_ATMP1, s3);
			break;



		/* METHOD INVOCATION *********************************************************/
		case ICMD_BUILTIN:      /* ..., [arg1, [arg2 ...]] ==> ...            */
			bte = iptr->sx.s23.s3.bte;
			if (bte->stub == NULL)
				disp = (ptrint) bte->fp;
			else
				disp = (ptrint) bte->stub;
			M_JSR_IMM(disp);
			break;

		case ICMD_INVOKESPECIAL:/* ..., objectref, [arg1, [arg2 ...]] ==> ... */
			/* adress register for sure */
			M_ALD(REG_ATMP1, REG_SP, 0);
			emit_nullpointer_check(cd, iptr, REG_ATMP1);
			/* fall through */

		case ICMD_INVOKESTATIC: /* ..., [arg1, [arg2 ...]] ==> ...            */
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				um = iptr->sx.s23.s3.um;
				patcher_add_patch_ref(jd, PATCHER_invokestatic_special, um, 0);
				disp = 0;
				M_AMOV_IMM(disp, REG_ATMP1);
			} else	{
				lm = iptr->sx.s23.s3.fmiref->p.method;
				disp = lm->stubroutine;
				M_AMOV_IMM(disp, REG_ATMP1);
			}

			/* generate the actual call */
			M_JSR(REG_ATMP1);
			break;

		case ICMD_INVOKEVIRTUAL:/* op1 = arg count, val.a = method pointer    */
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				um = iptr->sx.s23.s3.um;
				patcher_add_patch_ref(jd, PATCHER_invokevirtual, um, 0);
				s1 = 0;
			} else {
				lm = iptr->sx.s23.s3.fmiref->p.method;
				s1 = OFFSET(vftbl_t, table[0]) + sizeof(methodptr) * lm->vftblindex;
			}
			/* load object pointer (==argument 0) */
			M_ALD(REG_ATMP1, REG_SP, 0);
			/* implicit null-pointer check */
			M_ALD(REG_METHODPTR, REG_ATMP1, OFFSET(java_object_t, vftbl));
			M_ALD(REG_ATMP3, REG_METHODPTR, s1);
			/* generate the actual call */
			M_JSR(REG_ATMP3);
			break;

		case ICMD_INVOKEINTERFACE:
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				um = iptr->sx.s23.s3.um;
				patcher_add_patch_ref(jd, PATCHER_invokeinterface, um, 0);

				s1 = 0;
				s2 = 0;
			} else {
				lm = iptr->sx.s23.s3.fmiref->p.method;
				s1 = OFFSET(vftbl_t, interfacetable[0]) - sizeof(methodptr*) * lm->clazz->index;
				s2 = sizeof(methodptr) * (lm - lm->clazz->methods);
			}
			/* load object pointer (==argument 0) */
			M_ALD(REG_ATMP1, REG_SP, 0);

			/* implicit null-pointer check */
			M_ALD(REG_METHODPTR, REG_ATMP1, OFFSET(java_object_t, vftbl));
			M_ALD(REG_METHODPTR, REG_METHODPTR, s1);
			M_ALD(REG_ATMP3, REG_METHODPTR, s2);

			/* generate the actual call */
			M_JSR(REG_ATMP3);
			break;

XXXXXX

				switch (d)	{
					case TYPE_ADR:
						s1 = codegen_reg_of_dst(jd, iptr, REG_ATMP1);
						/* all stuff is returned in %d0 */
						M_INT2ADRMOVE(REG_RESULT, s1);
						break;
#if !defined(ENABLE_SOFTFLOAT)
					/*
					 *	for BUILTINS float values are returned in %d0,%d1
					 *	within cacao we use %fp0 for that.
					 */
					case TYPE_FLT:
						s1 = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
						if (iptr->opc == ICMD_BUILTIN)	{
							M_INT2FLTMOVE(REG_FRESULT, s1);
						} else	{
							emit_fmove(cd, REG_FRESULT, s1);
						}
						break;
					case TYPE_DBL:
						s1 = codegen_reg_of_dst(jd, iptr, REG_FTMP1);
						if (iptr->opc == ICMD_BUILTIN)	{
							M_LST(REG_RESULT_PACKED, REG_SP, rd->memuse * 4 + 4);
							M_DLD(s1, REG_SP, rd->memuse * 4 + 4);
						} else	{
							emit_dmove(cd, REG_FRESULT, s1);
						}
						break;
#endif
				}

XXXXXXX

		/* the evil ones */
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
			s4         superindex;

			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				super      = NULL;
				superindex = 0;
			}
			else {
				super      = iptr->sx.s23.s3.c.cls;
				superindex = super->index;
			}
			
			s1 = emit_load_s1(jd, iptr, REG_ATMP1);
			d = codegen_reg_of_dst(jd, iptr, REG_ITMP2);

			assert(VAROP(iptr->s1 )->type == TYPE_ADR);
			assert(VAROP(iptr->dst)->type == TYPE_INT);

			M_ICLR(d);

			/* if class is not resolved, check which code to call */

			if (super == NULL) {
				M_ATST(s1);
				emit_label_beq(cd, BRANCH_LABEL_1);

				patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_flags, iptr->sx.s23.s3.c.ref, 0);

				M_IMOV_IMM32(0, REG_ITMP3);
				M_IAND_IMM(ACC_INTERFACE, REG_ITMP3);
				emit_label_beq(cd, BRANCH_LABEL_2);
			}

			/* interface instanceof code */

			if ((super == NULL) || (super->flags & ACC_INTERFACE)) {
				if (super == NULL) {
					patcher_add_patch_ref(jd, PATCHER_instanceof_interface, iptr->sx.s23.s3.c.ref, 0);
				} else {
					M_ATST(s1);
					emit_label_beq(cd, BRANCH_LABEL_3);
				}

				M_ALD(REG_ATMP1, s1, OFFSET(java_object_t, vftbl));
				M_ILD(REG_ITMP3, REG_ATMP1, OFFSET(vftbl_t, interfacetablelength));
				M_IADD_IMM(-superindex, REG_ITMP3);	/* -superindex may be patched patched */
				M_ITST(REG_ITMP3);
				M_BLE(10);
				M_ALD(REG_ATMP1, REG_ATMP1, OFFSET(vftbl_t, interfacetable[0]) - superindex * sizeof(methodptr*));	/* patch here too! */
				M_ATST(REG_ATMP1);
				M_BEQ(2);
				M_IMOV_IMM(1, d);

				if (super == NULL)
					emit_label_br(cd, BRANCH_LABEL_4);
				else
					emit_label(cd, BRANCH_LABEL_3);
			}

			/* class instanceof code */

			if ((super == NULL) || !(super->flags & ACC_INTERFACE)) {
				if (super == NULL) {
					emit_label(cd, BRANCH_LABEL_2);

					patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_vftbl, iptr->sx.s23.s3.c.ref, 0);
					M_AMOV_IMM(0, REG_ATMP2);
				} else {
					M_AMOV_IMM(super->vftbl, REG_ATMP2);
					M_ATST(s1);
					emit_label_beq(cd, BRANCH_LABEL_5);
				}

				M_ALD(REG_ATMP1, s1, OFFSET(java_object_t, vftbl));

				M_ILD(REG_ITMP1, REG_ATMP1, OFFSET(vftbl_t, baseval));
				M_ILD(REG_ITMP3, REG_ATMP2, OFFSET(vftbl_t, baseval));
				M_ILD(REG_ITMP2, REG_ATMP2, OFFSET(vftbl_t, diffval));

				M_ISUB(REG_ITMP3, REG_ITMP1);
				M_ICMP(REG_ITMP2, REG_ITMP1);
				M_BHI(4);
				M_IMOV_IMM(1, d);
				M_TPFW;			/* overlaps next instruction */
				M_ICLR(d);

				if (super != NULL)
					emit_label(cd, BRANCH_LABEL_5);
			}

			if (super == NULL) {
				emit_label(cd, BRANCH_LABEL_1);
				emit_label(cd, BRANCH_LABEL_4);
			}

			emit_store_dst(jd, iptr, d);
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
				s4         superindex;

				if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
					super      = NULL;
					superindex = 0;
				}
				else {
					super      = iptr->sx.s23.s3.c.cls;
					superindex = super->index;
				}

				s1 = emit_load_s1(jd, iptr, REG_ATMP1);
				assert(VAROP(iptr->s1)->type == TYPE_ADR);

				/* if class is not resolved, check which code to call */

				if (super == NULL) {
					M_ATST(s1);
					emit_label_beq(cd, BRANCH_LABEL_1);

					patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_flags, iptr->sx.s23.s3.c.ref, 0);
			
					M_IMOV_IMM32(0, REG_ITMP2);
					M_IAND_IMM(ACC_INTERFACE, REG_ITMP2);
					emit_label_beq(cd, BRANCH_LABEL_2);
				}

				/* interface checkcast code */

				if ((super == NULL) || (super->flags & ACC_INTERFACE)) {
					if (super == NULL) {
						patcher_add_patch_ref(jd, PATCHER_checkcast_interface, iptr->sx.s23.s3.c.ref, 0);
					} else {
						M_ATST(s1);
						emit_label_beq(cd, BRANCH_LABEL_3);
					}

					M_ALD(REG_ATMP2, s1, OFFSET(java_object_t, vftbl));
					M_ILD(REG_ITMP3, REG_ATMP2, OFFSET(vftbl_t, interfacetablelength));
	
					M_IADD_IMM(-superindex, REG_ITMP3);	/* superindex patched */
					M_ITST(REG_ITMP3);
					emit_classcast_check(cd, iptr, BRANCH_LE, REG_ITMP3, s1);

					M_ALD(REG_ATMP3, REG_ATMP2, OFFSET(vftbl_t, interfacetable[0]) - superindex * sizeof(methodptr*));	/* patched*/
					M_ATST(REG_ATMP3);
					emit_classcast_check(cd, iptr, BRANCH_EQ, REG_ATMP3, s1);

					if (super == NULL)
						emit_label_br(cd, BRANCH_LABEL_4);
					else
						emit_label(cd, BRANCH_LABEL_3);
				}

				/* class checkcast code */

				if ((super == NULL) || !(super->flags & ACC_INTERFACE)) {
					if (super == NULL) {
						emit_label(cd, BRANCH_LABEL_2);

						patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_vftbl, iptr->sx.s23.s3.c.ref, 0);
						M_AMOV_IMM(0, REG_ATMP3);
					} else {
						M_AMOV_IMM(super->vftbl, REG_ATMP3);
						M_ATST(s1);
						emit_label_beq(cd, BRANCH_LABEL_5);
					}

					M_ALD(REG_ATMP2, s1, OFFSET(java_object_t, vftbl));

					M_ILD(REG_ITMP3, REG_ATMP2, OFFSET(vftbl_t, baseval));	/* REG_ITMP3 == sub->vftbl->baseval */
					M_ILD(REG_ITMP1, REG_ATMP3, OFFSET(vftbl_t, baseval));
					M_ILD(REG_ITMP2, REG_ATMP3, OFFSET(vftbl_t, diffval));

					M_ISUB(REG_ITMP1, REG_ITMP3);
					M_ICMP(REG_ITMP2, REG_ITMP3);	/* XXX was CMPU */

					emit_classcast_check(cd, iptr, BRANCH_UGT, REG_ITMP3, s1); /* XXX was BRANCH_GT */

					if (super != NULL)
						emit_label(cd, BRANCH_LABEL_5);
				}

				if (super == NULL) {
					emit_label(cd, BRANCH_LABEL_1);
					emit_label(cd, BRANCH_LABEL_4);
				}

				d = codegen_reg_of_dst(jd, iptr, s1);
			} else {
				/* array type cast-check */

				s1 = emit_load_s1(jd, iptr, REG_ATMP2);

				if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
					patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_classinfo, iptr->sx.s23.s3.c.ref, 0);
					M_AMOV_IMM(0, REG_ATMP1);
				} else {
					M_AMOV_IMM(iptr->sx.s23.s3.c.cls, REG_ATMP1);
				}
	
				M_APUSH(REG_ATMP1);
				M_APUSH(s1);
				M_JSR_IMM(BUILTIN_arraycheckcast);
				M_AADD_IMM(2*4, REG_SP);		/* pop arguments off stack */
				M_ITST(REG_RESULT);
				emit_classcast_check(cd, iptr, BRANCH_EQ, REG_RESULT, s1);

				s1 = emit_load_s1(jd, iptr, REG_ITMP1);
				d = codegen_reg_of_dst(jd, iptr, s1);
			}
			assert(VAROP(iptr->dst)->type == TYPE_ADR);
			M_ADRMOVE(s1, d);
			emit_store_dst(jd, iptr, d);
			break;

		case ICMD_TABLESWITCH:  /* ..., index ==> ...                         */
			{
			s4 i, l;
			branch_target_t *table;

			table = iptr->dst.table;

			l = iptr->sx.s23.s2.tablelow;
			i = iptr->sx.s23.s3.tablehigh;
			
			s1 = emit_load_s1(jd, iptr, REG_ITMP1);
			M_INTMOVE(s1, REG_ITMP1);
			if (l != 0) M_ISUB_IMM(l, REG_ITMP1);

			i = i - l + 1;

			/* range check */
			M_ICMP_IMM(i - 1, REG_ITMP1);
			emit_bugt(cd, table[0].block);

			/* build jump table top down and use address of lowest entry */
			table += i;

			while (--i >= 0) {
				dseg_add_target(cd, table->block); 
				--table;
			}

			/* length of dataseg after last dseg_add_target is used by load */
			M_AMOV_IMM(0, REG_ATMP2);
			dseg_adddata(cd);

			M_ISSL_IMM(2, REG_ITMP1);			/* index * 4 == offset in table */
			M_AADDINT(REG_ITMP1, REG_ATMP2);		/* offset in table */
			M_AADD_IMM(-(cd->dseglen), REG_ATMP2);		/* start of table in dseg */
			M_ALD(REG_ATMP1, REG_ATMP2, 0);

			M_JMP(REG_ATMP1);
			ALIGNCODENOP;
			}
			break;

		case ICMD_MULTIANEWARRAY:/* ..., cnt1, [cnt2, ...] ==> ..., arrayref  */

			/* check for negative sizes and copy sizes to stack if necessary  */
			MCODECHECK((iptr->s1.argcount << 1) + 64);

			for (s1 = iptr->s1.argcount; --s1 >= 0;) {
				var = VAR(iptr->sx.s23.s2.args[s1]);

				/* Already Preallocated? */
				if (!(var->flags & PREALLOC)) {
					s2 = emit_load(jd, iptr, var, REG_ITMP1);
					M_IST(s2, REG_SP, (s1 + 3) * 4);
				}
			}

			/* a0 = dimension count */
			M_IMOV_IMM(iptr->s1.argcount, REG_ITMP1);
			M_IST(REG_ITMP1, REG_SP, 0*4);

			/* a1 = arraydescriptor */
			if (INSTRUCTION_IS_UNRESOLVED(iptr)) {
				patcher_add_patch_ref(jd, PATCHER_resolve_classref_to_classinfo, iptr->sx.s23.s3.c.ref, 0);
				M_AMOV_IMM(0, REG_ATMP1);
			} else	{
				M_AMOV_IMM(iptr->sx.s23.s3.c.cls, REG_ATMP1);
			}
			M_AST(REG_ATMP1, REG_SP, 1*4);

			/* a2 = pointer to dimensions = stack pointer */
			M_AMOV(REG_SP, REG_ATMP1);
			M_AADD_IMM(3*4, REG_ATMP1);
			M_AST(REG_ATMP1, REG_SP, 2*4);

			M_JSR_IMM(BUILTIN_multianewarray);

			/* check for exception before result assignment */
			emit_exception_check(cd, iptr);

			assert(VAROP(iptr->dst)->type == TYPE_ADR);
			d = codegen_reg_of_dst(jd, iptr, REG_RESULT);
			M_INT2ADRMOVE(REG_RESULT, d);
			emit_store_dst(jd, iptr, d);
			break;



		default:
			vm_abort("Unknown ICMD %d during code generation", iptr->opc);
	} /* switch */
}


/* codegen_emit_stub_native ****************************************************

   Emits a stub routine which calls a native method.

*******************************************************************************/

void codegen_emit_stub_native(jitdata *jd, methoddesc *nmd, functionptr f, int skipparams)
{
	methodinfo   *m;
	codeinfo     *code;
	codegendata  *cd;
	registerdata *rd;
	methoddesc   *md;
	s4 i, j, t, s1, s2;
	
	/* get required compiler data */

	m    = jd->m;
	code = jd->code;
	cd   = jd->cd;
	rd   = jd->rd;

	md = m->parseddesc;

	/* calc stackframe size */
	cd->stackframesize =
		sizeof(stackframeinfo_t) / SIZEOF_VOID_P +
		sizeof(localref_table) / SIZEOF_VOID_P +
		nmd->memuse +
		1 +						/* functionptr */
		4;						/* args for codegen_start_native_call */

	/* create method header */
	(void) dseg_add_unique_address(cd, code);                      /* CodeinfoPointer */
	(void) dseg_add_unique_s4(cd, cd->stackframesize * 8);         /* FrameSize       */
	(void) dseg_add_unique_s4(cd, 0);                              /* IsLeaf          */
	(void) dseg_add_unique_s4(cd, 0);                              /* IntSave         */
	(void) dseg_add_unique_s4(cd, 0);                              /* FltSave         */

	/* generate code */
	M_AADD_IMM(-(cd->stackframesize*8), REG_SP);

	/* put arguments for codegen_start_native_call onto stack */
	/* void codegen_start_native_call(u1 *datasp, u1 *pv, u1 *sp, u1 *ra) */
	
	M_AMOV(REG_SP, REG_ATMP1);
	M_AST(REG_ATMP1, REG_SP, 0 * 4);		/* currentsp */

	M_AMOV_IMM(0, REG_ATMP2);			/* 0 needs to patched */
	dseg_adddata(cd);				    /* this patches it */

	M_AST(REG_ATMP2, REG_SP, 1 * 4);		/* pv */

	M_JSR_IMM(codegen_start_native_call);

	/* remember class argument */
	if (m->flags & ACC_STATIC)
		M_INT2ADRMOVE(REG_RESULT, REG_ATMP3);

	/* copy arguments into stackframe */
	for (i = md->paramcount -1, j = i + skipparams; i >= 0; --i, --j)	{
		t = md->paramtypes[i].type;
		/* all arguments via stack */
		assert(md->params[i].inmemory);						

		s1 = md->params[i].regoff + cd->stackframesize * 8 + 4;
		s2 = nmd->params[j].regoff;

		/* simply copy argument stack */
		M_ILD(REG_ITMP1, REG_SP, s1);
		M_IST(REG_ITMP1, REG_SP, s2);
		if (IS_2_WORD_TYPE(t))	{
			M_ILD(REG_ITMP1, REG_SP, s1 + 4);
			M_IST(REG_ITMP1, REG_SP, s2 + 4);
		}
	}

	/* builtins are not invoked like natives, environemtn and clazz are only needed for natives */
	if (m->flags & ACC_NATIVE)	{
		/* for static function class as second arg */
		if (m->flags & ACC_STATIC)
			M_AST(REG_ATMP3, REG_SP, 1 * 4);

		/* env ist first argument */
		M_AMOV_IMM(VM_get_jnienv(), REG_ATMP1);
		M_AST(REG_ATMP1, REG_SP, 0 * 4);
	}

	/* call the native function */
	M_AMOV_IMM(f, REG_ATMP2);
	M_JSR(REG_ATMP2);

	/* save return value */
	switch (md->returntype.type)	{
		case TYPE_VOID: break;

		/* natives return float arguments in %d0, %d1, cacao expects them in %fp0 */
		case TYPE_DBL:
		case TYPE_LNG:
			M_IST(REG_D1, REG_SP, 2 * 8);
			/* fall through */

		case TYPE_FLT:
		case TYPE_INT:
		case TYPE_ADR:
			M_IST(REG_D0, REG_SP, 2 * 8);	/* XXX can this be correct ? */
			break;

		default: assert(0);
	}

	/* remove native stackframe info */
	/* therefore we call: java_objectheader *codegen_finish_native_call(u1 *datasp) */

	M_AMOV(REG_SP, REG_ATMP1);
	M_AST(REG_ATMP1, REG_SP, 0 * 4);		/* currentsp */

	M_AMOV_IMM(0, REG_ATMP2);			/* 0 needs to patched */
	dseg_adddata(cd);				    /* this patches it */

	M_AST(REG_ATMP2, REG_SP, 1 * 4);		/* pv */

	M_JSR_IMM(codegen_finish_native_call);
	
	M_INT2ADRMOVE(REG_RESULT, REG_ATMP1);
	/* restore return value */
	switch (md->returntype.type)	{
		case TYPE_VOID: break;

		case TYPE_DBL:
		case TYPE_LNG:		M_ILD(REG_D1, REG_SP, 2 * 8);
			/* fall through */
		case TYPE_FLT:
		case TYPE_INT:
		case TYPE_ADR:
			M_ILD(REG_D0, REG_SP, 2 * 8);	/* XXX */
			break;

		default: assert(0);
	}
#if !defined(ENABLE_SOFTFLOAT)
		/* additionally load values into floating points registers
		 * as cacao jit code expects them there */
	switch (md->returntype.type)	{
		case TYPE_FLT:
			M_FLD(REG_D0, REG_SP, 2 * 8);
			break;
		case TYPE_DBL:	
			M_DLD(REG_D0, REG_SP, 2 * 8);	/* XXX */
			break;
	}
#endif
	/* restore saved registers */

	M_AADD_IMM(cd->stackframesize*8, REG_SP);
	/* check for exception */
	M_ATST(REG_ATMP1);
	M_BNE(2);
	M_RET;

	/* handle exception, REG_ATMP1 already contains exception object, REG_ATMP2 holds address */
	
	M_ALD(REG_ATMP2_XPC, REG_SP, 0);		/* take return address as faulting instruction */
	M_AADD_IMM(-2, REG_ATMP2_XPC);			/* which is off by 2 */
	M_JMP_IMM(asm_handle_nat_exception);

	/* should never be reached from within jit code*/
	M_JSR_IMM(0);
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
