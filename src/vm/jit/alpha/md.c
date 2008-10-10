/* src/vm/jit/alpha/md.c - machine dependent Alpha functions

   Copyright (C) 1996-2005, 2006, 2007, 2008
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

#include <stdint.h>
#include <ucontext.h>

#if defined(__LINUX__)
# include <asm/fpu.h>

extern unsigned long ieee_get_fp_control();
extern void ieee_set_fp_control(unsigned long fp_control);
#endif

#include "vm/jit/alpha/codegen.h"
#include "vm/jit/alpha/md.h"

#include "vm/jit/asmpart.h"
#include "vm/jit/jit.hpp"
#include "vm/jit/trap.h"


/* global variables ***********************************************************/

bool has_ext_instr_set = false;             /* has instruction set extensions */


/* md_init *********************************************************************

   Do some machine dependent initialization.

*******************************************************************************/

void md_init(void)
{
#if defined(__LINUX__)
	unsigned long int fpcw;
#endif

	/* check for extended instruction set */

	has_ext_instr_set = !asm_md_init();

#if defined(__LINUX__)
	/* Linux on Digital Alpha needs an initialisation of the ieee
	   floating point control for IEEE compliant arithmetic (option
	   -mieee of GCC). Under Digital Unix this is done
	   automatically. */

	/* initialize floating point control */

	fpcw = ieee_get_fp_control();

	fpcw = fpcw
		& ~IEEE_TRAP_ENABLE_INV
		& ~IEEE_TRAP_ENABLE_DZE
		/* We dont want underflow. */
/* 		& ~IEEE_TRAP_ENABLE_UNF */
		& ~IEEE_TRAP_ENABLE_OVF;

/* 	fpcw = fpcw */
/* 		| IEEE_TRAP_ENABLE_INV */
/* 		| IEEE_TRAP_ENABLE_DZE */
/* 		| IEEE_TRAP_ENABLE_OVF */
/* 		| IEEE_TRAP_ENABLE_UNF */
/* 		| IEEE_TRAP_ENABLE_INE */
/* 		| IEEE_TRAP_ENABLE_DNO; */

	ieee_set_fp_control(fpcw);
#endif
}


/* md_jit_method_patch_address *************************************************

   Gets the patch address of the currently compiled method. The offset
   is extracted from the load instruction(s) before the jump and added
   to the right base address (PV or REG_METHODPTR).

   INVOKESTATIC/SPECIAL:

   a77bffb8    ldq     pv,-72(pv)
   6b5b4000    jsr     (pv)

   INVOKEVIRTUAL:

   a7900000    ldq     at,0(a0)
   a77c0000    ldq     pv,0(at)
   6b5b4000    jsr     (pv)

   INVOKEINTERFACE:

   a7900000    ldq     at,0(a0)
   a79cff98    ldq     at,-104(at)
   a77c0018    ldq     pv,24(at)
   6b5b4000    jsr     (pv)

*******************************************************************************/

void *md_jit_method_patch_address(void *pv, void *ra, void *mptr)
{
	uint32_t *pc;
	uint32_t  mcode;
	int       opcode;
	int       base;
	int32_t   disp;
	void     *pa;                       /* patch address                      */

	/* Go back to the load instruction (2 instructions). */

	pc = ((uint32_t *) ra) - 2;

	/* Get first instruction word. */

	mcode = pc[0];

	/* Get opcode, base register and displacement. */

	opcode = M_MEM_GET_Opcode(mcode);
	base   = M_MEM_GET_Rb(mcode);
	disp   = M_MEM_GET_Memory_disp(mcode);

	/* Check for short or long load (2 instructions). */

	switch (opcode) {
	case 0x29: /* LDQ: TODO use define */
		switch (base) {
		case REG_PV:
			/* Calculate the data segment address. */

			pa = ((uint8_t *) pv) + disp;
			break;

		case REG_METHODPTR:
			/* Return NULL if no mptr was specified (used for
			   replacement). */

			if (mptr == NULL)
				return NULL;

			/* Calculate the address in the vftbl. */

			pa = ((uint8_t *) mptr) + disp;
			break;

		default:
			vm_abort_disassemble(pc, 2, "md_jit_method_patch_address: unknown instruction %x", mcode);
			return NULL;
		}
		break;

	case 0x09: /* LDAH: TODO use define */
		/* XXX write a regression for this */

		vm_abort("md_jit_method_patch_address: IMPLEMENT ME!");

		pa = NULL;
		break;

	default:
		vm_abort_disassemble(pc, 2, "md_jit_method_patch_address: unknown instruction %x", mcode);
		return NULL;
	}

	return pa;
}


/* md_patch_replacement_point **************************************************

   Patch the given replacement point.

*******************************************************************************/

#if defined(ENABLE_REPLACEMENT)
void md_patch_replacement_point(u1 *pc, u1 *savedmcode, bool revert)
{
	u4 mcode;

	if (revert) {
		/* restore the patched-over instruction */
		*(u4*)(pc) = *(u4*)(savedmcode);
	}
	else {
		/* save the current machine code */
		*(u4*)(savedmcode) = *(u4*)(pc);

		/* build the machine code for the patch */
		mcode = (0xa41f0000 | (TRAP_PATCHER));

		/* write the new machine code */
		*(u4*)(pc) = mcode;
	}
	
	/* flush instruction cache */
    md_icacheflush(pc,4);
}
#endif /* defined(ENABLE_REPLACEMENT) */


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
