/* src/mm/cacao-gc/gc.h - main garbage collector header

   Copyright (C) 2006 R. Grafl, A. Krall, C. Kruegel,
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

   Authors: Michael Starzinger

   $Id$

*/


#ifdef GC_CONST
# error Why is the BoehmGC header included???
#endif


#ifndef _GC_H
#define _GC_H


#include "config.h"
#include "vm/types.h"

#if defined(ENABLE_THREADS)
# include "threads/native/threads.h"
#endif

#include "vm/jit/replace.h"


/* Configuration Switches *****************************************************/

#define GCCONF_FINALIZER
/*#define GCCONF_HDRFLAG_REFERENCING*/


/* Debugging ******************************************************************/

#define GC_DEBUGGING
/*#define GC_DEBUGGING2*/

#if !defined(NDEBUG) && defined(GC_DEBUGGING)
# include <assert.h>
# include "vmcore/options.h"
# define GC_LOG(code) if (opt_verbosegc) { code; }
# define GC_ASSERT(assertion) assert(assertion)
#else
# define GC_LOG(code)
# define GC_ASSERT(assertion)
#endif

#if !defined(NDEBUG) && defined(GC_DEBUGGING2)
# define GC_LOG2(code) GC_LOG(code)
#else
# define GC_LOG2(code)
#endif


/* Development Break **********************************************************/

#if 0 && defined(ENABLE_THREADS)
# error "GC does not work with threads enabled!"
#endif

#if 1 && defined(ENABLE_INTRP)
# error "GC does not work with interpreter enabled!"
#endif

#if 1 && defined(ENABLE_JVMTI)
# error "GC does not work with JVMTI enabled!"
#endif

#if 1 && !defined(ENABLE_REPLACEMENT)
# error "GC does only work with replacement enabled!"
#endif

#if 1 && !defined(__I386__)
# error "GC was only ported to i386 so far!"
#endif


/* Helper Macros **************************************************************/

#define GC_SET_FLAGS(obj, flags)   ((obj)->hdrflags |=  (flags))
#define GC_CLEAR_FLAGS(obj, flags) ((obj)->hdrflags &= ~(flags))
#define GC_TEST_FLAGS(obj, flags)  ((obj)->hdrflags  &  (flags))

#define POINTS_INTO(ptr, ptr_start, ptr_end) \
	((void *) (ptr) >= (ptr_start) && (void *) (ptr) < (ptr_end))


/* Global Variables ***********************************************************/

extern bool gc_pending;
extern bool gc_notify_finalizer;


/* No-Thread specific stuff ***************************************************/

#if defined(ENABLE_THREADS)
# define GC_EXECUTIONSTATE (thread->es)
# define GC_SOURCESTATE    (thread->ss)
#else
# define GC_EXECUTIONSTATE (_no_threads_executionstate)
# define GC_SOURCESTATE    (_no_threads_sourcestate)

extern executionstate_t *_no_threads_executionstate;
extern sourcestate_t    *_no_threads_sourcestate;

#endif


/* Prototypes *****************************************************************/

#if defined(ENABLE_THREADS)
bool gc_suspend(threadobject *thread, u1 *pc, u1 *sp);
#endif


/* Statistics *****************************************************************/

#if defined(ENABLE_STATISTICS)

#define GCSTAT_INIT(cnt)  { (cnt) = 0; }
#define GCSTAT_COUNT(cnt) { (cnt)++; }
#define GCSTAT_DEC(cnt)   { (cnt)--; GC_ASSERT((cnt) >= 0); }
#define GCSTAT_COUNT_MAX(cnt,max) { (cnt)++; if ((cnt) > (max)) (max) = (cnt); }

extern int gcstat_mark_depth;
extern int gcstat_mark_depth_max;
extern int gcstat_mark_count;

void gcstat_println();

#endif /* defined(ENABLE_STATISTICS) */


#endif /* _GC_H */

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
