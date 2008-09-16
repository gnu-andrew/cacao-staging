/* src/vm/jit/powerpc/linux/md-os.c - machine dependent PowerPC Linux functions

   Copyright (C) 1996-2005, 2006, 2007, 2008
   CACAOVM - Verein zur Foerderung der freien virtuellen Maschine CACAO
   Copyright (C) 2008 Theobroma Systems Ltd.

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
#include <ucontext.h>

#include "vm/types.h"

#include "vm/jit/powerpc/codegen.h"
#include "vm/jit/powerpc/md.h"
#include "vm/jit/powerpc/linux/md-abi.h"

#include "threads/thread.hpp"

#include "vm/jit/builtin.hpp"
#include "vm/signallocal.h"
#include "vm/os.hpp"

#include "vm/jit/asmpart.h"
#include "vm/jit/disass.h"
#include "vm/jit/executionstate.h"

#if defined(ENABLE_PROFILING)
# include "vm/jit/optimizing/profile.h"
#endif

#include "vm/jit/patcher-common.hpp"
#include "vm/jit/trap.h"


/* md_signal_handler_sigsegv ***************************************************

   Signal handler for hardware-exceptions.

*******************************************************************************/

void md_signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t     *_uc;
	mcontext_t     *_mc;
	unsigned long  *_gregs;
	u1             *pv;
	u1             *sp;
	u1             *ra;
	u1             *xpc;
	u4              mcode;
	int             s1;
	int16_t         disp;
	int             d;
	intptr_t        addr;
	intptr_t        val;
	int             type;

 	_uc = (ucontext_t *) _p;

#if defined(__UCLIBC__)
	_mc    = &(_uc->uc_mcontext);
	_gregs = _mc->regs->gpr;
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	pv  = (u1 *) _gregs[REG_PV];
	sp  = (u1 *) _gregs[REG_SP];
	ra  = (u1 *) _gregs[PT_LNK];                 /* this is correct for leafs */
	xpc = (u1 *) _gregs[PT_NIP];

	/* get exception-throwing instruction */

	mcode = *((u4 *) xpc);

	s1   = M_INSTR_OP2_IMM_A(mcode);
	disp = M_INSTR_OP2_IMM_I(mcode);
	d    = M_INSTR_OP2_IMM_D(mcode);

	val  = _gregs[d];

	/* check for special-load */

	if (s1 == REG_ZERO) {
		/* we use the exception type as load displacement */

		type = disp;

		if (type == TRAP_COMPILER) {
			/* The XPC is the RA minus 4, because the RA points to the
			   instruction after the call. */

			xpc = ra - 4;
		}
	}
	else {
		/* This is a normal NPE: addr must be NULL and the NPE-type
		   define is 0. */

		addr = _gregs[s1];
		type = addr;
	}

	/* Handle the trap. */

	trap_handle(type, val, pv, sp, ra, xpc, _p);
}


/**
 * Signal handler for patcher calls.
 */
void md_signal_handler_sigill(int sig, siginfo_t* siginfo, void* _p)
{
	ucontext_t* _uc = (ucontext_t*) _p;
	mcontext_t* _mc;
	unsigned long* _gregs;

#if defined(__UCLIBC__)
	_mc    = &(_uc->uc_mcontext);
	_gregs = _mc->regs->gpr;
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	/* get register values */

	void* pv = (void*) _gregs[REG_PV];
	void* sp = (void*) _gregs[REG_SP];
	void* ra = (void*) _gregs[PT_LNK]; // The RA is correct for leag methods.
	void* xpc =(void*) _gregs[PT_NIP];

	// Get the illegal-instruction.
	uint32_t mcode = *((uint32_t*) xpc);

	// Check if the trap instruction is valid.
	// TODO Move this into patcher_handler.
	if (patcher_is_valid_trap_instruction_at(xpc) == false) {
		// Check if the PC has been patched during our way to this
		// signal handler (see PR85).
		if (patcher_is_patched_at(xpc) == true)
			return;

		// We have a problem...
		log_println("md_signal_handler_sigill: Unknown illegal instruction 0x%x at 0x%lx", mcode, xpc);
#if defined(ENABLE_DISASSEMBLER)
		(void) disassinstr(xpc);
#endif
		vm_abort("Aborting...");
	}

	// This signal is always a patcher.
	int      type = TRAP_PATCHER;
	intptr_t val  = 0;

	// Handle the trap.
	trap_handle(type, val, pv, sp, ra, xpc, _p);
}


/* md_signal_handler_sigtrap ***************************************************

   Signal handler for hardware-traps.

*******************************************************************************/

void md_signal_handler_sigtrap(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t     *_uc;
	mcontext_t     *_mc;
	unsigned long  *_gregs;
	u1             *pv;
	u1             *sp;
	u1             *ra;
	u1             *xpc;
	u4              mcode;
	int             s1;
	intptr_t        val;
	int             type;

 	_uc = (ucontext_t *) _p;

#if defined(__UCLIBC__)
	_mc    = &(_uc->uc_mcontext);
	_gregs = _mc->regs->gpr;
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	pv  = (u1 *) _gregs[REG_PV];
	sp  = (u1 *) _gregs[REG_SP];
	ra  = (u1 *) _gregs[PT_LNK];                 /* this is correct for leafs */
	xpc = (u1 *) _gregs[PT_NIP];

	/* get exception-throwing instruction */

	mcode = *((u4 *) xpc);

	s1 = M_OP3_GET_A(mcode);

	/* For now we only handle ArrayIndexOutOfBoundsException. */

	type = TRAP_ArrayIndexOutOfBoundsException;
	val  = _gregs[s1];

	/* Handle the trap. */

	trap_handle(type, val, pv, sp, ra, xpc, _p);
}


/* md_signal_handler_sigusr1 ***************************************************

   Signal handler for suspending threads.

*******************************************************************************/

#if defined(ENABLE_THREADS) && defined(ENABLE_GC_CACAO)
void md_signal_handler_sigusr1(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t    *_uc;
	mcontext_t    *_mc;
	unsigned long *_gregs;
	u1            *pc;
	u1            *sp;

	_uc = (ucontext_t *) _p;

#if defined(__UCLIBC__)
	_mc    = &(_uc->uc_mcontext);
	_gregs = _mc->regs->gpr;
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	/* get the PC and SP for this thread */

	pc = (u1 *) _gregs[PT_NIP];
	sp = (u1 *) _gregs[REG_SP];

	/* now suspend the current thread */

	threads_suspend_ack(pc, sp);
}
#endif


/* md_signal_handler_sigusr2 ***************************************************

   Signal handler for profiling sampling.

*******************************************************************************/

#if defined(ENABLE_THREADS)
void md_signal_handler_sigusr2(int sig, siginfo_t *siginfo, void *_p)
{
	threadobject  *tobj;
	ucontext_t    *_uc;
	mcontext_t    *_mc;
	unsigned long *_gregs;
	u1            *pc;

	tobj = THREADOBJECT;

 	_uc = (ucontext_t *) _p;

#if defined(__UCLIBC__)
	_mc    = &(_uc->uc_mcontext);
	_gregs = _mc->regs->gpr;
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	pc = (u1 *) _gregs[PT_NIP];

	tobj->pc = pc;
}
#endif


/* md_executionstate_read ******************************************************

   Read the given context into an executionstate.

*******************************************************************************/

void md_executionstate_read(executionstate_t *es, void *context)
{
	ucontext_t    *_uc;
	mcontext_t    *_mc;
	unsigned long *_gregs;
	s4              i;

	_uc = (ucontext_t *) context;

#if defined(__UCLIBC__)
#error Please port md_executionstate_read to __UCLIBC__
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	/* read special registers */
	es->pc = (u1 *) _gregs[PT_NIP];
	es->sp = (u1 *) _gregs[REG_SP];
	es->pv = (u1 *) _gregs[REG_PV];
	es->ra = (u1 *) _gregs[PT_LNK];

	/* read integer registers */
	for (i = 0; i < INT_REG_CNT; i++)
		es->intregs[i] = _gregs[i];

	/* read float registers */
	/* Do not use the assignment operator '=', as the type of
	 * the _mc->fpregs[i] can cause invalid conversions. */

	assert(sizeof(_mc->fpregs.fpregs) == sizeof(es->fltregs));
	os_memcpy(&es->fltregs, &_mc->fpregs.fpregs, sizeof(_mc->fpregs.fpregs));
}


/* md_executionstate_write *****************************************************

   Write the given executionstate back to the context.

*******************************************************************************/

void md_executionstate_write(executionstate_t *es, void *context)
{
	ucontext_t    *_uc;
	mcontext_t    *_mc;
	unsigned long *_gregs;
	s4              i;

	_uc = (ucontext_t *) context;

#if defined(__UCLIBC__)
#error Please port md_executionstate_write to __UCLIBC__
#else
	_mc    = _uc->uc_mcontext.uc_regs;
	_gregs = _mc->gregs;
#endif

	/* write integer registers */
	for (i = 0; i < INT_REG_CNT; i++)
		_gregs[i] = es->intregs[i];

	/* write float registers */
	/* Do not use the assignment operator '=', as the type of
	 * the _mc->fpregs[i] can cause invalid conversions. */

	assert(sizeof(_mc->fpregs.fpregs) == sizeof(es->fltregs));
	os_memcpy(&_mc->fpregs.fpregs, &es->fltregs, sizeof(_mc->fpregs.fpregs));

	/* write special registers */
	_gregs[PT_NIP] = (ptrint) es->pc;
	_gregs[REG_SP] = (ptrint) es->sp;
	_gregs[REG_PV] = (ptrint) es->pv;
	_gregs[PT_LNK] = (ptrint) es->ra;
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
 */
