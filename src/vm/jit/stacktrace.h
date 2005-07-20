/* src/vm/jit/stacktrace.h - header file for stacktrace generation

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

   $Id: stacktrace.h 3073 2005-07-20 10:40:41Z twisti $

*/


#ifndef _STACKTRACE_H
#define _STACKTRACE_H

/* forward typedefs ***********************************************************/

typedef struct stackframeinfo stackframeinfo;
typedef struct stackTraceBuffer stackTraceBuffer;
typedef struct stacktraceelement stacktraceelement;

#include "config.h"
#include "types.h"

#include "vm/method.h"


/* stackframeinfo *************************************************************/

struct stackframeinfo {
	void        *oldThreadspecificHeadValue;
	void       **addressOfThreadspecificHead;
	methodinfo  *method;                /* methodinfo of current function     */
	u1          *pv;                    /* PV of current function             */
	u1          *sp;                    /* SP of parent Java function         */
	functionptr  ra;                    /* RA to parent Java function         */
	functionptr  xpc;                   /* XPC (for inline stubs)             */
	void        *padding;               /* multiple of 16-byte padding        */
};


struct stacktraceelement {
#if SIZEOF_VOID_P == 8
	u8          linenumber;
#else
	u4          linenumber;
#endif
	methodinfo *method;
};


struct stackTraceBuffer {
	s4                 needsFree;
	stacktraceelement *start;
	s4                 size;
	s4                 full;
};


/* function prototypes ********************************************************/

void stacktrace_create_inline_stackframeinfo(stackframeinfo *sfi, u1 *pv,
											 u1 *sp, functionptr ra,
											 functionptr xpc);

void stacktrace_create_extern_stackframeinfo(stackframeinfo *sfi, u1 *pv,
											 u1 *sp, functionptr ra,
											 functionptr xpc);

void stacktrace_create_native_stackframeinfo(stackframeinfo *sfi, u1 *pv,
											 u1 *sp, functionptr ra);

void stacktrace_remove_stackframeinfo(stackframeinfo *sfi);

/* inline exception creating functions */
java_objectheader *stacktrace_inline_arithmeticexception(u1 *pv, u1 *sp,
														 functionptr ra,
														 functionptr xpc);

java_objectheader *stacktrace_inline_arrayindexoutofboundsexception(u1 *pv,
																	u1 *sp,
																	functionptr ra,
																	functionptr xpc,
																	s4 index);

java_objectheader *stacktrace_inline_arraystoreexception(u1 *pv, u1 *sp,
														 functionptr ra,
														 functionptr xpc);

java_objectheader *stacktrace_inline_classcastexception(u1 *pv, u1 *sp,
														functionptr ra,
														functionptr xpc);

java_objectheader *stacktrace_inline_negativearraysizeexception(u1 *pv, u1 *sp,
																functionptr ra,
																functionptr xpc);

java_objectheader *stacktrace_inline_nullpointerexception(u1 *pv, u1 *sp,
														  functionptr ra,
														  functionptr xpc);


/* hardware exception creating functions */
java_objectheader *stacktrace_hardware_arithmeticexception(u1 *pv, u1 *sp,
														   functionptr ra,
														   functionptr xpc);

java_objectheader *stacktrace_hardware_nullpointerexception(u1 *pv, u1 *sp,
															functionptr ra,
															functionptr xpc);

/* refill the stacktrace of an existing exception */
java_objectheader *stacktrace_inline_fillInStackTrace(u1 *pv, u1 *sp,
													  functionptr ra,
													  functionptr xpc);

void cacao_stacktrace_NormalTrace(void **target);
java_objectarray *cacao_createClassContextArray(void);
java_objectheader *cacao_currentClassLoader(void);
methodinfo* cacao_callingMethod(void);
java_objectarray *cacao_getStackForVMAccessController(void);
void stacktrace_dump_trace(void);

/* machine dependent functions (code in ARCH_DIR/md.c) */
functionptr md_stacktrace_get_returnaddress(u1 *sp, u4 framesize);

#endif /* _STACKTRACE_H */


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
