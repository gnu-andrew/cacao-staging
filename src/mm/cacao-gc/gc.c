/* src/mm/cacao-gc/gc.c - main garbage collector methods

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

   $Id$

*/


#include "config.h"
#include <signal.h>
#include "vm/types.h"

#if defined(ENABLE_THREADS)
# include "threads/native/threads.h"
#else
/*# include "threads/none/threads.h"*/
#endif

#include "compact.h"
#include "copy.h"
#include "final.h"
#include "gc.h"
#include "heap.h"
#include "mark.h"
#include "region.h"
#include "rootset.h"
#include "mm/memory.h"
#include "toolbox/logging.h"
#include "vm/finalizer.h"
#include "vm/vm.h"
#include "vmcore/rt-timing.h"


/* Global Variables ***********************************************************/

bool gc_pending;
bool gc_running;
bool gc_notify_finalizer;

#if !defined(ENABLE_THREADS)
executionstate_t *_no_threads_executionstate;
sourcestate_t    *_no_threads_sourcestate;
#endif


/* gc_init *********************************************************************

   Initializes the garbage collector.

*******************************************************************************/

#define GC_SYS_SIZE (20*1024*1024)

void gc_init(u4 heapmaxsize, u4 heapstartsize)
{
	if (opt_verbosegc)
		dolog("GC: Initialising with heap-size %d (max. %d)",
			heapstartsize, heapmaxsize);

	/* finalizer stuff */
	final_init();

	/* set global variables */
	gc_pending = false;
	gc_running = false;

	/* region for uncollectable objects */
	heap_region_sys = NEW(regioninfo_t);
	if (!region_create(heap_region_sys, GC_SYS_SIZE))
		vm_abort("gc_init: region_create failed: out of memory");

	/* region for java objects */
	heap_region_main = NEW(regioninfo_t);
	if (!region_create(heap_region_main, heapstartsize))
		vm_abort("gc_init: region_create failed: out of memory");

	heap_current_size = heapstartsize;
	heap_maximal_size = heapmaxsize;
}


/* gc_collect ******************************************************************

   This is the main machinery which manages a collection. It should be run by
   the thread which triggered the collection.

   IN:
     XXX

   STEPS OF A COLLECTION:
     XXX

*******************************************************************************/

void gc_collect(s4 level)
{
	rootset_t    *rs;
	s4            dumpsize;
#if defined(ENABLE_RT_TIMING)
	struct timespec time_start, time_suspend, time_rootset, time_mark, time_compact, time_end;
#endif

	/* this is only quick'n'dirty check, but is NOT thread safe */
	if (gc_pending || gc_running) {
		GC_LOG( dolog("GC: Preventing reentrance!"); );
		return;
	}

	/* TODO: some global GC lock!!! */

	/* remember start of dump memory area */
	dumpsize = dump_size();

	RT_TIMING_GET_TIME(time_start);

	/* let everyone know we want to do a collection */
	GC_ASSERT(!gc_pending);
	gc_pending = true;

	/* finalizer is not notified, unless marking tells us to do so */
	gc_notify_finalizer = false;

#if defined(ENABLE_THREADS)
	/* stop the world here */
	GC_LOG( dolog("GC: Suspending threads ..."); );
	GC_LOG( threads_dump(); );
	threads_stopworld();
	GC_LOG( threads_dump(); );
	GC_LOG( dolog("GC: Suspension finished."); );
#endif

	/* sourcestate of the current thread */
	replace_gc_from_native(THREADOBJECT, NULL, NULL);

	/* everyone is halted now, we consider ourselves running */
	GC_ASSERT(!gc_running);
	gc_pending = false;
	gc_running = true;

	RT_TIMING_GET_TIME(time_suspend);

	GC_LOG( heap_println_usage(); );
	/*GC_LOG( heap_dump_region(heap_region_main, false); );*/

	/* find the global and local rootsets */
	rs = rootset_readout();
	GC_LOG( rootset_print(rs); );

	RT_TIMING_GET_TIME(time_rootset);

#if 1

	/* mark the objects considering the given rootset */
	mark_me(rs);
	/*GC_LOG( heap_dump_region(heap_region_main, true); );*/

	RT_TIMING_GET_TIME(time_mark);

	/* compact the heap */
	compact_me(rs, heap_region_main);
	/*GC_LOG( heap_dump_region(heap_region_main, false); );
	GC_LOG( rootset_print(rs); );*/

#if defined(ENABLE_MEMCHECK)
	/* invalidate the rest of the main region */
	region_invalidate(heap_region_main);
#endif

	RT_TIMING_GET_TIME(time_compact);

#else

	/* copy the heap to new region */
	{
		regioninfo_t *src, *dst;

		src = heap_region_main;
		dst = NEW(regioninfo_t);
		region_create(dst, heap_current_size);
		copy_me(heap_region_main, dst, rs);
		heap_region_main = dst;

		/* invalidate old heap */
		memset(src->base, 0x66, src->size);
	}
#endif

	/* TODO: check my return value! */
	/*heap_increase_size();*/

	/* write back the rootset to update root references */
	GC_LOG( rootset_print(rs); );
	rootset_writeback(rs);

#if defined(ENABLE_STATISTICS)
	if (opt_verbosegc)
		gcstat_println();
#endif

#if defined(GCCONF_FINALIZER)
	/* does the finalizer need to be notified */
	if (gc_notify_finalizer)
		finalizer_notify();
#endif

	/* we are no longer running */
	/* REMEBER: keep this below the finalizer notification */
	gc_running = false;

#if defined(ENABLE_THREADS)
	/* start the world again */
	GC_LOG( dolog("GC: Reanimating world ..."); );
	threads_startworld();
	/*GC_LOG( threads_dump(); );*/
#endif

	RT_TIMING_GET_TIME(time_end);

	RT_TIMING_TIME_DIFF(time_start  , time_suspend, RT_TIMING_GC_SUSPEND);
	RT_TIMING_TIME_DIFF(time_suspend, time_rootset, RT_TIMING_GC_ROOTSET1)
	RT_TIMING_TIME_DIFF(time_rootset, time_mark   , RT_TIMING_GC_MARK);
	RT_TIMING_TIME_DIFF(time_mark   , time_compact, RT_TIMING_GC_COMPACT);
	RT_TIMING_TIME_DIFF(time_compact, time_end    , RT_TIMING_GC_ROOTSET2);
	RT_TIMING_TIME_DIFF(time_start  , time_end    , RT_TIMING_GC_TOTAL);

    /* free dump memory area */
    dump_release(dumpsize);

}


#if defined(ENABLE_THREADS)
bool gc_suspend(threadobject *thread, u1 *pc, u1 *sp)
{
	codeinfo         *code;

	/* check if the thread suspended itself */
	if (pc == NULL) {
		GC_LOG( dolog("GC: Suspended myself!"); );
		return true;
	}

	/* thread was forcefully suspended */
	GC_LOG( dolog("GC: Suspending thread (tid=%p)", thread->tid); );

	/* check where this thread came to a halt */
	if (thread->flags & THREAD_FLAG_IN_NATIVE) {

		if (thread->gc_critical) {
			GC_LOG( dolog("\tNATIVE &  CRITICAL -> retry"); );

			GC_ASSERT(0);

			/* wait till this thread suspends itself */
			return false;

		} else {
			GC_LOG( dolog("\tNATIVE & SAFE -> suspend"); );

			/* we assume we are in a native! */
			replace_gc_from_native(thread, pc, sp);

			/* suspend me now */
			return true;

		}

	} else {
		code = code_find_codeinfo_for_pc_nocheck(pc);

		if (code != NULL) {
			GC_LOG( dolog("\tJIT (pc=%p) & KNOWN(codeinfo=%p) -> replacement",
					pc, code); );

			/* arm the replacement points of the code this thread is in */
			replace_activate_replacement_points(code, false);

			/* wait till this thread suspends itself */
			return false;

		} else {
			GC_LOG( dolog("\tJIT (pc=%p) & UN-KNOWN -> retry", pc); );

			/* re-suspend me later */
			/* TODO: implement me! */
			/* TODO: (this is a rare race condition which was not yet triggered) */
			GC_ASSERT(0);
			return false;

		}

	}

	/* this point should never be reached */
	GC_ASSERT(0);

}
#endif


/* gc_call *********************************************************************

   Forces a full collection of the whole Java Heap.
   This is the function which is called by System.VMRuntime.gc()

*******************************************************************************/

void gc_call(void)
{
	if (opt_verbosegc)
		dolog("GC: Forced Collection ...");

	gc_collect(0);

	if (opt_verbosegc)
		dolog("GC: Forced Collection finished.");
}


/* gc_invoke_finalizers ********************************************************

   Forces invocation of all the finalizers for objects which are reclaimable.
   This is the function which is called by the finalizer thread.

*******************************************************************************/

void gc_invoke_finalizers(void)
{
	if (opt_verbosegc)
		dolog("GC: Invoking finalizers ...");

	final_invoke();

	if (opt_verbosegc)
		dolog("GC: Invoking finalizers finished.");
}


/* Informational getter functions *********************************************/

s8 gc_get_heap_size(void)     { return heap_current_size; }
s8 gc_get_free_bytes(void)    { return heap_region_main->free; }
s8 gc_get_total_bytes(void)   { return heap_region_main->size - heap_region_main->free; }
s8 gc_get_max_heap_size(void) { return heap_maximal_size; }


/* Statistics *****************************************************************/

#if defined(ENABLE_STATISTICS)
int gcstat_mark_depth;
int gcstat_mark_depth_max;
int gcstat_mark_count;

void gcstat_println()
{
    printf("\nGCSTAT - Marking Statistics:\n");
    printf("\t# of objects marked: %d\n", gcstat_mark_count);
    printf("\tMaximal marking depth: %d\n", gcstat_mark_depth_max);

	printf("\nGCSTAT - Compaction Statistics:\n");

	printf("\n");
}
#endif /* defined(ENABLE_STATISTICS) */


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
