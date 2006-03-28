/* src/vm/jit/x86_64/md.c - machine dependent x86_64 Linux functions

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

   Changes: Edwin Steiner

   $Id: md.c 4698 2006-03-28 14:31:53Z twisti $

*/


#define _GNU_SOURCE

#include "config.h"

#include <stdlib.h>
#include <ucontext.h>

#include "vm/jit/x86_64/md-abi.h"

#include "vm/exceptions.h"
#include "vm/signallocal.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/stacktrace.h"

#if !defined(NDEBUG) && defined(ENABLE_DISASSEMBLER)
#include "vm/options.h" /* XXX debug */
#include "vm/jit/disass.h" /* XXX debug */
#endif


/* md_init *********************************************************************

   Do some machine dependent initialization.

*******************************************************************************/

void md_init(void)
{
	/* nothing to do */
}


/* md_signal_handler_sigsegv ***************************************************

   NullPointerException signal handler for hardware null pointer
   check.

*******************************************************************************/

void md_signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t  *_uc;
	mcontext_t  *_mc;
	u1          *sp;
	u1          *ra;
	u1          *xpc;

	_uc = (ucontext_t *) _p;
	_mc = &_uc->uc_mcontext;

	/* ATTENTION: Don't use CACAO's internal REG_* defines as they are
	   different to the ones in <ucontext.h>. */

	sp  = (u1 *) _mc->gregs[REG_RSP];
	xpc = (u1 *) _mc->gregs[REG_RIP];
	ra  = xpc;                          /* return address is equal to xpc     */

	_mc->gregs[REG_RAX] =
		(ptrint) stacktrace_hardware_nullpointerexception(NULL, sp, ra, xpc);

	_mc->gregs[REG_R10] = (ptrint) xpc;                      /* REG_ITMP2_XPC */
	_mc->gregs[REG_RIP] = (ptrint) asm_handle_exception;
}


/* md_signal_handler_sigfpe ****************************************************

   ArithmeticException signal handler for hardware divide by zero
   check.

*******************************************************************************/

void md_signal_handler_sigfpe(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t  *_uc;
	mcontext_t  *_mc;
	u1          *sp;
	u1          *ra;
	u1          *xpc;

	_uc = (ucontext_t *) _p;
	_mc = &_uc->uc_mcontext;

	/* ATTENTION: Don't use CACAO's internal REG_* defines as they are
	   different to the ones in <ucontext.h>. */

	sp  = (u1 *) _mc->gregs[REG_RSP];
	xpc = (u1 *) _mc->gregs[REG_RIP];
	ra  = xpc;                          /* return address is equal to xpc     */

	_mc->gregs[REG_RAX] =
		(ptrint) stacktrace_hardware_arithmeticexception(NULL, sp, ra, xpc);

	_mc->gregs[REG_R10] = (ptrint) xpc;                      /* REG_ITMP2_XPC */
	_mc->gregs[REG_RIP] = (ptrint) asm_handle_exception;
}


#if defined(USE_THREADS) && defined(NATIVE_THREADS)
void thread_restartcriticalsection(ucontext_t *uc)
{
	void *critical;

	critical = thread_checkcritical((void *) uc->uc_mcontext.gregs[REG_RIP]);

	if (critical)
		uc->uc_mcontext.gregs[REG_RIP] = (ptrint) critical;
}
#endif


/* md_stacktrace_get_returnaddress *********************************************

   Returns the return address of the current stackframe, specified by
   the passed stack pointer and the stack frame size.

*******************************************************************************/

u1 *md_stacktrace_get_returnaddress(u1 *sp, u4 framesize)
{
	u1 *ra;

	/* on x86_64 the return address is above the current stack frame */

	ra = *((u1 **) (sp + framesize));

	return ra;
}


/* md_codegen_findmethod *******************************************************

   On this architecture just a wrapper function to codegen_findmethod.

*******************************************************************************/

u1 *md_codegen_findmethod(u1 *ra)
{
	u1 *pv;

	/* the the start address of the function which contains this
       address from the method table */

	pv = codegen_findmethod(ra);

	return pv;
}


/* md_cacheflush ***************************************************************

   Calls the system's function to flush the instruction and data
   cache.

*******************************************************************************/

void md_cacheflush(u1 *addr, s4 nbytes)
{
	/* do nothing */
}


/* md_patch_replacement_point **************************************************

   Patch the given replacement point.

*******************************************************************************/

void md_patch_replacement_point(rplpoint *rp)
{
    u8 mcode;

	/* XXX this is probably unsafe! */

	/* save the current machine code */
	mcode = *(u8*)rp->pc;

	/* write spinning instruction */
	*(u2*)(rp->pc) = 0xebfe;

	/* write 5th byte */
	rp->pc[4] = (rp->mcode >> 32);

	/* write first word */
    *(u4*)(rp->pc) = (u4) rp->mcode;

	/* store saved mcode */
	rp->mcode = mcode;
	
#if !defined(NDEBUG) && defined(ENABLE_DISASSEMBLER)
	{
		u1* u1ptr = rp->pc;
		DISASSINSTR(u1ptr);
		fflush(stdout);
	}
#endif
			
    /* XXX if required asm_cacheflush(rp->pc,8); */
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
