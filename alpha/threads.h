/****************************** threads.h **************************************

	Copyright (c) 1997 A. Krall, R. Grafl, M. Gschwind, M. Probst

	See file COPYRIGHT for information on usage and disclaimer of warranties

	System dependent part of thread header file.

	Authors: Mark Probst         EMAIL: cacao@complang.tuwien.ac.at
	         Andreas  Krall      EMAIL: cacao@complang.tuwien.ac.at

	Last Change: 1998/11/19

*******************************************************************************/


#ifndef __sysdep_threads_h
#define __sysdep_threads_h

#include "../threads/thread.h"

/* Thread handling */

/* prototypes */

void asm_perform_threadswitch (u1 **from, u1 **to, u1 **stackTop);
u1*  asm_initialize_thread_stack (void *func, u1 *stack);
void asm_switchstackandcall (void *stack, void *func, void **stacktopsave);

/* access macros */

#define	THREADSTACKSIZE         (32 * 1024)

#define	THREADSWITCH(to, from)	asm_perform_threadswitch(&(from)->restorePoint,\
                                    &(to)->restorePoint, &(from)->usedStackTop)

#define THREADINIT(to, func)    (to)->restorePoint =                         \
                                    asm_initialize_thread_stack((u1*)(func), \
                                                            (to)->stackEnd)

#define	THREADINFO(e)                               \
		do {                                        \
			(e)->restorePoint = 0;                  \
			(e)->flags = THREAD_FLAGS_NOSTACKALLOC; \
		} while(0)

#endif
