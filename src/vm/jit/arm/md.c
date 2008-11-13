/* src/vm/jit/arm/md.c - machine dependent ARM functions

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

#include <assert.h>
#include <stdint.h>

#include "vm/jit/arm/codegen.h"
#include "vm/jit/arm/md.h"
#include "vm/jit/arm/md-abi.h"


/* md_init *********************************************************************

   Do some machine dependent initialization.

*******************************************************************************/

void md_init(void)
{
	/* do nothing here */
}


/* md_jit_method_patch_address *************************************************

   Gets the patch address of the currently compiled method. The offset
   is extracted from the load instruction(s) before the jump and added
   to the right base address (PV or REG_METHODPTR).

   Machine code:

   e51cc040    ldr   ip, [ip, #-64]
   e1a0e00f    mov   lr, pc
   e1a0f00c    mov   pc, ip

   or

   e590b000    ldr   fp, [r0]
   e59bc004    ldr   ip, [fp, #4]
   e1a0e00f    mov   lr, pc
   e1a0f00c    mov   pc, ip

   or

   e590b000    ldr	fp, [r0]
   e28bca01    add	ip, fp, #4096	; 0x1000
   e59cc004    ldr	ip, [ip, #4]
   e1a0e00f    mov	lr, pc
   e1a0f00c    mov	pc, ip

   How we find out the patching address to store new method pointer:
    - loaded IP with LDR IP,[METHODPTR]?
        yes=INVOKEVIRTUAL or INVOKEINTERFACE (things are easy!)
    - loaded IP from data segment
        yes=INVOKESTATIC or INVOKESPECIAL (things are complicated)
        recompute pointer to data segment, maybe larger offset 

*******************************************************************************/

void *md_jit_method_patch_address(void *pv, void *ra, void *mptr)
{
	uint32_t *pc;
	uint32_t  mcode;
	int32_t   disp;
	void     *pa;                       /* patch address                      */

	/* Go back to the actual load instruction. */

	pc = ((uint32_t *) ra) - 3;

	/* Get first instruction word on current PC. */

	mcode = pc[0];

	/* Sanity check: Are we inside jit code? */

	assert(pc[1] == 0xe1a0e00f /*MOV LR,PC*/);
	assert(pc[2] == 0xe1a0f00c /*MOV PC,IP*/);

	/* Sanity check: We unconditionally loaded a word into REG_PV? */

	assert ((mcode & 0xff70f000) == 0xe510c000);

	/* Get load displacement. */

	disp = (int32_t) (mcode & 0x0fff);

	/* Case: We loaded from base REG_PV with negative displacement. */

	if (M_MEM_GET_Rbase(mcode) == REG_PV && (mcode & 0x00800000) == 0) {
		/* We loaded from data segment, displacement can be larger. */

		mcode = pc[-1];

		/* check for "SUB IP, IP, #??, ROTL 12" */

		if ((mcode & 0xffffff00) == 0xe24cca00)
			disp += (int32_t) ((mcode & 0x00ff) << 12);

		/* and get the final data segment address */

		pa = ((uint8_t *) pv) - disp;
	}

	/* Case: We loaded from base REG_METHODPTR with positive displacement. */

	else if (M_MEM_GET_Rbase(mcode) == REG_METHODPTR && (mcode & 0x00800000) == 0x00800000) {
		/* return NULL if no mptr was specified (used for replacement) */

		if (mptr == NULL)
			return NULL;

		/* we loaded from REG_METHODPTR */

		pa = ((uint8_t *) mptr) + disp;
	}

	/* Case: We loaded from base REG_PV with positive offset. */

	else if (M_MEM_GET_Rbase(mcode) == REG_PV && (mcode & 0x00800000) == 0x00800000) {
		/* We loaded from REG_METHODPTR with a larger displacement */

		mcode = pc[-1];

		/* check for "ADD IP, FP, #??, ROTL 12" */

		if ((mcode & 0xffffff00) == 0xe28bca00)
			disp += (int32_t) ((mcode & 0x00ff) << 12);
		else
			vm_abort_disassemble(pc - 1, 4, "md_jit_method_patch_address: unknown instruction %x", mcode);

		/* we loaded from REG_METHODPTR */

		pa = ((uint8_t *) mptr) + disp;
	}

	/* Case is not covered, something is severely wrong. */

	else {
		vm_abort_disassemble(pc, 3, "md_jit_method_patch_address: unknown instruction %x", mcode);

		/* Keep compiler happy. */

		pa = NULL;
	}

	return pa;
}


/**
 * Patch the given replacement point.
 */
#if defined(ENABLE_REPLACEMENT)
void md_patch_replacement_point(u1 *pc, u1 *savedmcode, bool revert)
{
	vm_abort("md_patch_replacement_point: IMPLEMENT ME!");
}
#endif


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
