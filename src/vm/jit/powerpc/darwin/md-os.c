/* src/vm/jit/powerpc/darwin/md-os.c - machine dependent PowerPC Darwin functions

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

   $Id: md-os.c 8283 2007-08-09 15:10:05Z twisti $

*/


#include "config.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>

#include "vm/types.h"

#include "vm/jit/powerpc/codegen.h"
#include "vm/jit/powerpc/darwin/md-abi.h"

#if defined(ENABLE_THREADS)
# include "threads/native/threads.h"
#endif

#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/signallocal.h"
#include "vm/stringlocal.h"

#include "vm/jit/asmpart.h"
#include "vm/jit/stacktrace.h"


/* md_signal_handler_sigsegv ***************************************************

   NullPointerException signal handler for hardware null pointer
   check.

*******************************************************************************/

void md_signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p)
{
	stackframeinfo      sfi;
	ucontext_t         *_uc;
	mcontext_t          _mc;
	ppc_thread_state_t *_ss;
	ptrint             *gregs;
	u1                 *pv;
	u1                 *sp;
	u1                 *ra;
	u1                 *xpc;
	u4                  mcode;
	int                 s1;
	int16_t             disp;
	int                 d;
	intptr_t            addr;
	intptr_t            val;
	int                 type;
	void               *p;

	_uc = (ucontext_t *) _p;
	_mc = _uc->uc_mcontext;
	_ss = &_mc->ss;

	/* immitate a gregs array */

	gregs = &_ss->r0;

	/* get register values */

	pv  = (u1 *) _ss->r13;
	sp  = (u1 *) _ss->r1;
	ra  = (u1 *) _ss->lr;                    /* this is correct for leafs */
	xpc = (u1 *) _ss->srr0;

	/* get exception-throwing instruction */

	mcode = *((u4 *) xpc);

	s1   = M_INSTR_OP2_IMM_A(mcode);
	disp = M_INSTR_OP2_IMM_I(mcode);
	d    = M_INSTR_OP2_IMM_D(mcode);

	val  = gregs[d];

	/* check for special-load */

	if (s1 == REG_ZERO) {
		/* we use the exception type as load displacement */

		type = disp;
	}
	else {
		/* This is a normal NPE: addr must be NULL and the NPE-type
		   define is 0. */

		addr = gregs[s1];
		type = EXCEPTION_HARDWARE_NULLPOINTER;

		if (addr != 0)
			vm_abort("md_signal_handler_sigsegv: faulting address is not NULL: addr=%p", addr);
	}

	/* create stackframeinfo */

	stacktrace_create_extern_stackframeinfo(&sfi, pv, sp, ra, xpc);

	/* Handle the type. */

	p = signal_handle(xpc, type, val);

	/* remove stackframeinfo */

	stacktrace_remove_stackframeinfo(&sfi);

	/* set registers (only if exception object ready) */

	if (p != NULL) {
		_ss->r11  = (intptr_t) p;
		_ss->r12  = (intptr_t) xpc;
		_ss->srr0 = (intptr_t) asm_handle_exception;
	}
}


/* md_signal_handler_sigtrap ***************************************************

   Signal handler for hardware-traps.

*******************************************************************************/

void md_signal_handler_sigtrap(int sig, siginfo_t *siginfo, void *_p)
{
	stackframeinfo      sfi;
	ucontext_t         *_uc;
	mcontext_t          _mc;
	ppc_thread_state_t *_ss;
	ptrint             *gregs;
	u1                 *pv;
	u1                 *sp;
	u1                 *ra;
	u1                 *xpc;
	u4                  mcode;
	int                 s1;
	intptr_t            val;
	int                 type;
	void               *p;

 	_uc = (ucontext_t *) _p;
	_mc = _uc->uc_mcontext;
	_ss = &_mc->ss;

	/* immitate a gregs array */

	gregs = &_ss->r0;

	/* get register values */

	pv  = (u1 *) _ss->r13;
	sp  = (u1 *) _ss->r1;
	ra  = (u1 *) _ss->lr;                    /* this is correct for leafs */
	xpc = (u1 *) _ss->srr0;

	/* get exception-throwing instruction */

	mcode = *((u4 *) xpc);

	s1 = M_OP3_GET_A(mcode);

	/* for now we only handle ArrayIndexOutOfBoundsException */

	type = EXCEPTION_HARDWARE_ARRAYINDEXOUTOFBOUNDS;
	val  = gregs[s1];

	/* create stackframeinfo */

	stacktrace_create_extern_stackframeinfo(&sfi, pv, sp, ra, xpc);

	/* Handle the type. */

	p = signal_handle(xpc, type, val);

	/* remove stackframeinfo */

	stacktrace_remove_stackframeinfo(&sfi);

	/* set registers */

	_ss->r11  = (intptr_t) p;
	_ss->r12  = (intptr_t) xpc;
	_ss->srr0 = (intptr_t) asm_handle_exception;
}


/* md_signal_handler_sigusr2 ***************************************************

   Signal handler for profiling sampling.

*******************************************************************************/

void md_signal_handler_sigusr2(int sig, siginfo_t *siginfo, void *_p)
{
	threadobject       *t;
	ucontext_t         *_uc;
	mcontext_t          _mc;
	ppc_thread_state_t *_ss;
	u1                 *pc;

	t = THREADOBJECT;

	_uc = (ucontext_t *) _p;
	_mc = _uc->uc_mcontext;
	_ss = &_mc->ss;

	pc = (u1 *) _ss->srr0;

	t->pc = pc;
}


/* md_critical_section_restart *************************************************

   Search the critical sections tree for a matching section and set
   the PC to the restart point, if necessary.

*******************************************************************************/

#if defined(ENABLE_THREADS)
void md_critical_section_restart(ucontext_t *_uc)
{
	mcontext_t          _mc;
	ppc_thread_state_t *_ss;
	u1                 *pc;
	u1                 *npc;

	_mc = _uc->uc_mcontext;
	_ss = &_mc->ss;

	pc = (u1 *) _ss->srr0;

	npc = critical_find_restart_point(pc);

	if (npc != NULL)
		_ss->srr0 = (ptrint) npc;
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
