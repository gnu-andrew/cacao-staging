/* src/vm/jit/powerpc/darwin/md.c - machine dependent PowerPC Darwin functions

   Copyright (C) 1996-2005 R. Grafl, A. Krall, C. Kruegel, C. Oates,
   R. Obermaisser, M. Platter, M. Probst, S. Ring, E. Steiner,
   C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich, J. Wenninger,
   Institut f. Computersprachen - TU Wien

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   Contact: cacao@complang.tuwien.ac.at

   Authors: Christian Thalinger

   Changes:

   $Id: md.c 2830 2005-06-25 15:32:34Z twisti $

*/


#include <signal.h>
#include <ucontext.h>

#include "config.h"

#include "vm/jit/powerpc/types.h"
#include "vm/jit/powerpc/darwin/md-abi.h"

#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/stringlocal.h"
#include "vm/jit/asmpart.h"


#if 0
/* cacao_catch_Handler *********************************************************

   XXX

*******************************************************************************/

int cacao_catch_Handler(mach_port_t thread)
{
#if defined(USE_THREADS)
	unsigned int *regs;
	unsigned int crashpc;
	s4 instr, reg;
/*	java_objectheader *xptr; */

	/* Mach stuff */
	thread_state_flavor_t flavor = PPC_THREAD_STATE;
	mach_msg_type_number_t thread_state_count = PPC_THREAD_STATE_COUNT;
	ppc_thread_state_t thread_state;
	kern_return_t r;
	
	if (checknull)
		return 0;

	r = thread_get_state(thread, flavor,
		(natural_t*)&thread_state, &thread_state_count);
	if (r != KERN_SUCCESS) {
		log_text("thread_get_state failed");
		assert(0);
	}

	regs = &thread_state.r0;
	crashpc = thread_state.srr0;

	instr = *(s4*) crashpc;
	reg = (instr >> 16) & 31;

	if (!regs[reg]) {
/*      This is now handled in asmpart because it needs to run in the throwing
 *      thread */
/*		xptr = new_nullpointerexception(); */

		regs[REG_ITMP2_XPC] = crashpc;
/*		regs[REG_ITMP1_XPTR] = (u4) xptr; */
		thread_state.srr0 = (u4) asm_handle_nullptr_exception;

		r = thread_set_state(thread, flavor,
			(natural_t*)&thread_state, thread_state_count);
		if (r != KERN_SUCCESS) {
			log_text("thread_set_state failed");
			assert(0);
		}

		return 1;
	}

	throw_cacao_exception_exit(string_java_lang_InternalError,
				   "Segmentation fault at %p", regs[reg]);
#endif

	return 0;
}
#endif


/* md_init *********************************************************************

   Do some machine dependent initialization.

*******************************************************************************/

void md_init(void)
{
	/* nothing to do */
}


/* signal_handler_sigsegv ******************************************************

   NullPointerException signal handler for hardware null pointer check.

*******************************************************************************/

void signal_handler_sigsegv(int sig, siginfo_t *siginfo, void *_p)
{
	ucontext_t         *uc;
	mcontext_t          mc;
	ppc_thread_state_t *ss;
	ptrint             *gregs;
	u4                  instr;
	s4                  reg;
	ptrint              addr;

	uc = (ucontext_t *) _p;
	mc = uc->uc_mcontext;
	ss = &mc->ss;

	/* check for NullPointerException */

	gregs = &ss->r0;

	instr = *((u4 *) ss->srr0);
	reg = (instr >> 16) & 31;
	addr = gregs[reg];

	if (addr == 0) {
		/* set the REG_ITMP1_XPTR, REG_ITMP2_XPC and new PC */

		ss->r11 = (ptrint) new_nullpointerexception();
		ss->r12 = ss->srr0;
		ss->srr0 = (ptrint) asm_handle_exception;

	} else {
		throw_cacao_exception_exit(string_java_lang_InternalError,
					   "Segmentation fault at 0x%08lx", addr);
	}
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
