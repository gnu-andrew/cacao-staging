/* src/vm/jit/arm/linux/md-os.c - machine dependent arm linux functions

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

#include "vm/jit/arm/md-abi.h"

#define ucontext broken_glibc_ucontext
#define ucontext_t broken_glibc_ucontext_t
#include <ucontext.h>
#undef ucontext
#undef ucontext_t

typedef struct ucontext {
   unsigned long     uc_flags;
   struct ucontext  *uc_link;
   stack_t           uc_stack;
   struct sigcontext uc_mcontext;
   sigset_t          uc_sigmask;
} ucontext_t;

#define scontext_t struct sigcontext

#if defined(ENABLE_THREADS)
# include "threads/native/threads.h"
#endif

#include "vm/exceptions.h"
#include "vm/signallocal.h"
#include "vm/stringlocal.h"

#include "vm/jit/asmpart.h"
#include "vm/jit/stacktrace.h"


/* md_signal_handler_sigsegv ***************************************************

   Signal handler for hardware exceptions.

*******************************************************************************/

void md_signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p)
{
	stackframeinfo  sfi;
	ucontext_t     *_uc;
	scontext_t     *_sc;
	u1             *pv;
	u1             *sp;
	u1             *ra;
	u1             *xpc;
	u4              mcode;
	intptr_t        addr;
	int             type;
	intptr_t        val;
	void           *p;

	_uc = (ucontext_t*) _p;
	_sc = &_uc->uc_mcontext;

	/* ATTENTION: glibc included messed up kernel headers we needed a
	   workaround for the ucontext structure. */

	pv  = (u1 *) _sc->arm_ip;
	sp  = (u1 *) _sc->arm_sp;
	ra  = (u1 *) _sc->arm_lr;                    /* this is correct for leafs */
	xpc = (u1 *) _sc->arm_pc;

	/* get exception-throwing instruction */

	if (xpc == NULL)
		vm_abort("md_signal_handler_sigsegv: the program counter is NULL");

	mcode = *((s4 *) xpc);

	/* this is a NullPointerException */

	addr = *((s4 *) _sc + OFFSET(scontext_t, arm_r0)/4 + ((mcode >> 16) & 0x0f));
	type = EXCEPTION_HARDWARE_NULLPOINTER;
	val  = 0;

	if (addr != 0)
		vm_abort("md_signal_handler_sigsegv: faulting address is not NULL: addr=%p", addr);

	/* create stackframeinfo */

	stacktrace_create_extern_stackframeinfo(&sfi, pv, sp, ra, xpc);

	/* Handle the type. */

	p = signal_handle(xpc, type, val);

	/* remove stackframeinfo */

	stacktrace_remove_stackframeinfo(&sfi);

	/* set registers */

	_sc->arm_r10 = (intptr_t) p;
	_sc->arm_fp  = (intptr_t) xpc;
	_sc->arm_pc  = (intptr_t) asm_handle_exception;
}


/* md_signal_handler_sigill ****************************************************

   Illegal Instruction signal handler for hardware exception checks.

*******************************************************************************/

void md_signal_handler_sigill(int sig, siginfo_t *siginfo, void *_p)
{
	stackframeinfo  sfi;
	ucontext_t     *_uc;
	scontext_t     *_sc;
	u1             *pv;
	u1             *sp;
	u1             *ra;
	u1             *xpc;
	u4              mcode;
	int             type;
	intptr_t        val;
	void           *p;

	_uc = (ucontext_t*) _p;
	_sc = &_uc->uc_mcontext;

	/* ATTENTION: glibc included messed up kernel headers we needed a
	   workaround for the ucontext structure. */

	pv  = (u1 *) _sc->arm_ip;
	sp  = (u1 *) _sc->arm_sp;
	ra  = (u1 *) _sc->arm_lr;                    /* this is correct for leafs */
	xpc = (u1 *) _sc->arm_pc;

	/* get exception-throwing instruction */

	mcode = *((u4 *) xpc);

	/* check for undefined instruction we use */

	if ((mcode & 0x0ff000f0) != 0x07f000f0)
		vm_abort("md_signal_handler_sigill: unknown illegal instruction");

	type = (mcode >> 8) & 0x0fff;
	val  = *((s4 *) _sc + OFFSET(scontext_t, arm_r0)/4 + (mcode & 0x0f));

	/* create stackframeinfo */

	stacktrace_create_extern_stackframeinfo(&sfi, pv, sp, ra, xpc);

	/* Handle the type. */

	p = signal_handle(xpc, type, val);

	/* remove stackframeinfo */

	stacktrace_remove_stackframeinfo(&sfi);

	/* set registers if we have an exception, return continue execution
	   otherwise (this is needed for patchers to work) */

	if (p != NULL) {
		_sc->arm_r10 = (intptr_t) p;
		_sc->arm_fp  = (intptr_t) xpc;
		_sc->arm_pc  = (intptr_t) asm_handle_exception;
	}
}


/* md_signal_handler_sigusr2 ***************************************************

   Signal handler for profiling sampling.

*******************************************************************************/

#if defined(ENABLE_THREADS)
void md_signal_handler_sigusr2(int sig, siginfo_t *siginfo, void *_p)
{
	threadobject *thread;
	ucontext_t   *_uc;
	scontext_t   *_sc;
	u1           *pc;

	thread = THREADOBJECT;

	_uc = (ucontext_t*) _p;
	_sc = &_uc->uc_mcontext;

	pc = (u1 *) _sc->arm_pc;

	thread->pc = pc;
}
#endif


/* md_critical_section_restart *************************************************

   Search the critical sections tree for a matching section and set
   the PC to the restart point, if necessary.

*******************************************************************************/

#if defined(ENABLE_THREADS)
void md_critical_section_restart(ucontext_t *_uc)
{
	scontext_t *_sc;
	u1         *pc;
	u1         *npc;

	_sc = &_uc->uc_mcontext;

	pc = (u1 *) _sc->arm_pc;

	npc = critical_find_restart_point(pc);

	if (npc != NULL)
		_sc->arm_pc = (ptrint) npc;
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
 */

