/*	src/vm/jit/m68k/md.c

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
#include <stdint.h>

#include "vm/types.h"

#include "vm/vm.hpp"


/*
 *	As a sanity measuremnt we assert the offset.h values in here as m68k gets
 *	crosscompiled for sure and noone thinks of offset.h wen changing compile flags
 *	and subtile bugs will result...
 *
 *	m68k uses the trap instruction for hardware exceptions, need to register
 *	according signal handler
 */
void md_init(void) 
{
}


/* md_jit_method_patch_address *************************************************
 
   Gets the patch address of the currently compiled method. Has to be 
   extracted from the load instructions which lead to the jump.

from asmpart.S (asm_vm_call_method):
84:   2879 0000 0000  moveal 0 <asm_vm_call_method-0x34>,%a4
8a:   4e94            jsr %a4@


from invokestatic / invokespecial
0x40290882:   247c 4029 03b4    moveal #1076429748,%a2
0x40290888:   4e92              jsr %a2@

from invokevirtual
0x40297eca:   266a 0000         moveal %a2@(0),%a3
0x40297ece:   246b 002c         moveal %a3@(44),%a2
0x40297ed2:   4e92              jsr %a2@



*******************************************************************************/

void *md_jit_method_patch_address(void *pv, void *ra, void *mptr)
{
	uint8_t *pc;
	int16_t  disp;
	void    *pa;

	pc = (uint8_t *) ra;

	if (*((u2*)(pc - 2)) == 0x4e94)	{		/* jsr %a4@ */
		if (*((u2*)(pc - 6)) == 0x286b)	{
			/* found an invokevirtual */
			/* get offset of load instruction 246b XXXX */
			disp = *((s2*)(pc - 4));

			/* return NULL if no mptr was specified (used for replacement) */

			if (mptr == NULL)
				return NULL;

			pa = ((uint8_t *) mptr) + disp;/* mptr contains the magic we want */
		} else	{
			/* we had a moveal XXX, %a3 which is a 3 word opcode */
			/* 2679 0000 0000 */
			assert(*(u2*)(pc - 8) == 0x2879);		/* moveal */
			pa = (void*)*((u4*)(pc - 6));				/* another indirection ! */
		}
	} else if (*((u2*)(pc - 2)) == 0x4e92)	{		/* jsr %a2@ */
		if (*(u2*)(pc - 8) == 0x247c)	{
			/* found a invokestatic/invokespecial */
			pa = ((u4*)(pc - 6));			/* no indirection ! */
		} else {
			assert(0);
		}
	} else {
		assert(0);
	}

	return pa;
}

void *md_stacktrace_get_returnaddress(void *sp, int32_t stackframesize)
{
	void *ra;

	/* return address is above stackpointer */

	ra = *((void **) (((uintptr_t) sp) + stackframesize));
	
	/* XXX: This helps for now, but it's a ugly hack
	 * the problem _may_ be: the link instruction is used
	 * by some gcc generated code, and we get an additional word
	 * on the stack, the old framepointer. Its address is somewhere
	 * near sp, but that all depends the code generated by the compiler.
	 * I'm unsure about a clean solution.
	 */
#if 0
	if (!(ra > 0x40000000 && ra < 0x80000000))      {
	        ra = *((u1**)(sp + framesize + 4));
	}
#endif

	/* assert(ra > 0x40000000 && ra < 0x80000000);
	printf("XXXXXX=%x\n", ra);
	 */

	return ra;
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
