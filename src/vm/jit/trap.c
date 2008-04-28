/* src/vm/jit/trap.c - hardware traps

   Copyright (C) 2008
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

/* Include machine dependent trap stuff. */

#include "md-trap.h"

#include "native/llni.h"

#include "toolbox/logging.h"

#include "vm/exceptions.h"
#include "vm/vm.h"

#include "vm/jit/code.h"
#include "vm/jit/disass.h"
#include "vm/jit/jit.h"
#include "vm/jit/methodtree.h"
#include "vm/jit/patcher-common.h"
#include "vm/jit/replace.h"
#include "vm/jit/stacktrace.h"

#include "vmcore/options.h"
#include "vmcore/system.h"


/**
 * Mmap the first memory page to support hardware exceptions and check
 * the maximum hardware trap displacement on the architectures where
 * it is required (TRAP_INSTRUCTION_IS_LOAD defined to 1).
 */
void trap_init(void)
{
#if !(defined(__ARM__) && defined(__LINUX__))
	/* On arm-linux the first memory page can't be mmap'ed, as it
	   contains the exception vectors. */

	int pagesize;

	/* mmap a memory page at address 0x0, so our hardware-exceptions
	   work. */

	pagesize = system_getpagesize();

	(void) system_mmap_anonymous(NULL, pagesize, PROT_NONE, MAP_PRIVATE | MAP_FIXED);
#endif

	TRACESUBSYSTEMINITIALIZATION("trap_init");

#if !defined(TRAP_INSTRUCTION_IS_LOAD)
# error TRAP_INSTRUCTION_IS_LOAD is not defined in your md-trap.h
#endif

#if TRAP_INSTRUCTION_IS_LOAD == 1
	/* Check if we get into trouble with our hardware-exceptions. */

	if (TRAP_END > OFFSET(java_bytearray_t, data))
		vm_abort("trap_init: maximum hardware trap displacement is greater than the array-data offset: %d > %d", TRAP_END, OFFSET(java_bytearray_t, data));
#endif
}


/**
 * Handles the signal which is generated by trap instructions, caught
 * by a signal handler and calls the correct function.
 *
 * @param type trap number
 * @param 
 */
void* trap_handle(int type, intptr_t val, void *pv, void *sp, void *ra, void *xpc, void *context)
{
	stackframeinfo_t  sfi;
	int32_t           index;
	java_handle_t    *o;
	methodinfo       *m;
	java_handle_t    *p;

#if !defined(NDEBUG)
	if (opt_TraceTraps)
		log_println("[signal_handle: trap %d]", type);
#endif
	
#if defined(ENABLE_VMLOG)
	vmlog_cacao_signl_type(type);
#endif

	/* Prevent compiler warnings. */

	o = NULL;
	m = NULL;

	/* wrap the value into a handle if it is a reference */
	/* BEFORE: creating stackframeinfo */

	switch (type) {
	case TRAP_ClassCastException:
		o = LLNI_WRAP((java_object_t *) val);
		break;

	case TRAP_COMPILER:
		/* In this case the passed PV points to the compiler stub.  We
		   get the methodinfo pointer here and set PV to NULL so
		   stacktrace_stackframeinfo_add determines the PV for the
		   parent Java method. */

		m  = code_get_methodinfo_for_pv(pv);
		pv = NULL;
		break;

	default:
		/* do nothing */
		break;
	}

	/* Fill and add a stackframeinfo. */

	stacktrace_stackframeinfo_add(&sfi, pv, sp, ra, xpc);

	switch (type) {
	case TRAP_NullPointerException:
		p = exceptions_new_nullpointerexception();
		break;

	case TRAP_ArithmeticException:
		p = exceptions_new_arithmeticexception();
		break;

	case TRAP_ArrayIndexOutOfBoundsException:
		index = (s4) val;
		p = exceptions_new_arrayindexoutofboundsexception(index);
		break;

	case TRAP_ArrayStoreException:
		p = exceptions_new_arraystoreexception();
		break;

	case TRAP_ClassCastException:
		p = exceptions_new_classcastexception(o);
		break;

	case TRAP_CHECK_EXCEPTION:
		p = exceptions_fillinstacktrace();
		break;

	case TRAP_PATCHER:
#if defined(ENABLE_REPLACEMENT)
		if (replace_me_wrapper(xpc, context)) {
			p = NULL;
			break;
		}
#endif
		p = patcher_handler(xpc);
		break;

	case TRAP_COMPILER:
		p = jit_compile_handle(m, sfi.pv, ra, (void *) val);
		break;

	default:
		/* Let's try to get a backtrace. */

		(void) methodtree_find(xpc);

		/* If that does not work, print more debug info. */

		log_println("signal_handle: unknown hardware exception type %d", type);

#if SIZEOF_VOID_P == 8
		log_println("PC=0x%016lx", xpc);
#else
		log_println("PC=0x%08x", xpc);
#endif

#if defined(ENABLE_DISASSEMBLER)
		log_println("machine instruction at PC:");
		disassinstr(xpc);
#endif

		vm_abort("Exiting...");

		/* keep compiler happy */

		p = NULL;
	}

	/* Remove stackframeinfo. */

	stacktrace_stackframeinfo_remove(&sfi);

	/* unwrap and return the exception object */
	/* AFTER: removing stackframeinfo */

	if (type == TRAP_COMPILER)
		return p;
	else
		return LLNI_UNWRAP(p);
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
