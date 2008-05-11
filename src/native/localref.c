/* src/native/localref.c - Management of local reference tables

   Copyright (C) 1996-2005, 2006, 2007, 2008
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

#include <assert.h>
#include <stdint.h>

#include "mm/memory.h"

#include "native/localref.h"

#include "threads/thread.h"

#include "toolbox/logging.h"

#include "vm/vm.h"

#include "vm/jit/argument.h"

#include "vmcore/options.h"


/* debug **********************************************************************/

#if !defined(NDEBUG)
# define DEBUGLOCALREF(message, index) \
	do { \
		if (opt_DebugLocalReferences) { \
			localref_table *dlrt = LOCALREFTABLE; \
			log_start(); \
			log_print("[local reference %-12s: lrt=%016p frame=%d capacity=%d used=%d", message, dlrt, dlrt->localframes, dlrt->capacity, dlrt->used); \
			if (index >= 0) \
				log_print(" localref=%p object=%p", &(dlrt->refs[index]), dlrt->refs[index]); \
			log_print("]"); \
			log_finish(); \
		} \
	} while (0)
#else
# define DEBUGLOCALREF(message, index)
#endif


/* global variables ***********************************************************/

#if !defined(ENABLE_THREADS)
localref_table *_no_threads_localref_table;
#endif


/* some forward declarations **************************************************/

#if !defined(NDEBUG)
static bool localref_check_uncleared();
#endif


/* localref_table_init *********************************************************

   Initializes the local references table of the current thread.

*******************************************************************************/

bool localref_table_init(void)
{
	localref_table *lrt;

	TRACESUBSYSTEMINITIALIZATION("localref_table_init");

	assert(LOCALREFTABLE == NULL);

#if !defined(ENABLE_GC_BOEHM)
	/* this is freed by localref_table_destroy */
	lrt = NEW(localref_table);
#else
	/* this does not need to be freed again */
	lrt = GCNEW(localref_table);
#endif

	if (lrt == NULL)
		return false;

	localref_table_add(lrt);

	DEBUGLOCALREF("table init", -1);

	return true;
}


/* localref_table_destroy ******************************************************

   Destroys the complete local references table of the current thread.

*******************************************************************************/

bool localref_table_destroy(void)
{
	localref_table *lrt;

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(lrt->prev == NULL);

	DEBUGLOCALREF("table destroy", -1);

#if !defined(ENABLE_GC_BOEHM)
	FREE(lrt, localref_table);
#endif

	LOCALREFTABLE = NULL;

	return true;
}


/* localref_table_add **********************************************************

   Adds a new local references table to the current thread.

*******************************************************************************/

void localref_table_add(localref_table *lrt)
{
	/* initialize the local reference table */

	lrt->capacity    = LOCALREFTABLE_CAPACITY;
	lrt->used        = 0;
	lrt->localframes = 1;
	lrt->prev        = LOCALREFTABLE;

	/* clear the references array (memset is faster the a for-loop) */

	MSET(lrt->refs, 0, void*, LOCALREFTABLE_CAPACITY);

	/* add given local references table to this thread */

	LOCALREFTABLE = lrt;

	/*DEBUGLOCALREF("table add", -1);*/
}


/* localref_table_remove *******************************************************

   Removes the topmost local references table from the current thread.

*******************************************************************************/

void localref_table_remove()
{
	localref_table *lrt;

#if !defined(NDEBUG)
	/* check for uncleared local references */

	localref_check_uncleared();
#endif

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(lrt->localframes == 1);

	/*DEBUGLOCALREF("table remove", -1);*/

	lrt = lrt->prev;

	LOCALREFTABLE = lrt;
}


/* localref_frame_push *********************************************************

   Creates a new local reference frame, in which at least a given
   number of local references can be created.

*******************************************************************************/

bool localref_frame_push(int32_t capacity)
{
	localref_table *lrt;
	localref_table *nlrt;
	int32_t         additionalrefs;

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(capacity > 0);

	/* Allocate new local reference table on Java heap.  Calculate the
	   additional memory we have to allocate. */

	if (capacity > LOCALREFTABLE_CAPACITY)
		additionalrefs = capacity - LOCALREFTABLE_CAPACITY;
	else
		additionalrefs = 0;

#if !defined(ENABLE_GC_BOEHM)
	nlrt = (localref_table *)
			MNEW(u1, sizeof(localref_table) + additionalrefs * SIZEOF_VOID_P);
#else
	nlrt = (localref_table *)
			GCMNEW(u1, sizeof(localref_table) + additionalrefs * SIZEOF_VOID_P);
#endif

	if (nlrt == NULL)
		return false;

	/* Set up the new local reference table and add it to the local
	   frames chain. */

	nlrt->capacity    = capacity;
	nlrt->used        = 0;
	nlrt->localframes = lrt->localframes + 1;
	nlrt->prev        = lrt;

	/* store new local reference table in thread */

	LOCALREFTABLE = nlrt;

	DEBUGLOCALREF("frame push", -1);

	return true;
}


/* localref_frame_pop_all ******************************************************

   Pops off all the local reference frames of the current table.

*******************************************************************************/

void localref_frame_pop_all(void)
{
	localref_table *lrt;
	localref_table *plrt;
	int32_t         localframes;
#if !defined(ENABLE_GC_BOEHM)
	int32_t         additionalrefs;
#endif

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);

	localframes = lrt->localframes;

	/* Don't delete the top local frame, as this one is allocated in
	   the native stub on the stack and is freed automagically on
	   return. */

	if (localframes == 1)
		return;

	/* release all current local frames */

	for (; localframes > 1; localframes--) {
		/* get previous frame */

		plrt = lrt->prev;

		DEBUGLOCALREF("frame pop", -1);

		/* clear all reference entries */

		MSET(lrt->refs, 0, void*, lrt->capacity);

		lrt->prev = NULL;

#if !defined(ENABLE_GC_BOEHM)
		/* for the exact GC local reference tables are not on the heap,
		   so we need to free them explicitly here. */

		if (lrt->capacity > LOCALREFTABLE_CAPACITY)
			additionalrefs = lrt->capacity - LOCALREFTABLE_CAPACITY;
		else
			additionalrefs = 0;

		MFREE(lrt, u1, sizeof(localref_table) + additionalrefs * SIZEOF_VOID_P);
#endif

		/* set new local references table */

		lrt = plrt;
	}

	/* store new local reference table in thread */

	LOCALREFTABLE = lrt;
}


/* localref_add ****************************************************************

   Adds a new entry into the local reference table and returns the
   new local reference.

*******************************************************************************/

java_handle_t *localref_add(java_object_t *o)
{
	localref_table *lrt;
	java_handle_t  *h;
	int32_t         i;

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(o != NULL);
	/* XXX: assert that we are in a GC critical section! */

	/* Check if we have space for the requested reference?  No,
	   allocate a new frame.  This is actually not what the spec says,
	   but for compatibility reasons... */

    if (lrt->used == lrt->capacity) {
		if (!localref_frame_push(64))
			assert(0);

		/* get the new local reference table */ 

		lrt = LOCALREFTABLE;
	}

	/* insert the reference into the local reference table */

	for (i = 0; i < lrt->capacity; i++) {
		if (lrt->refs[i] == NULL) {
			lrt->refs[i] = o;
			lrt->used++;

#if defined(ENABLE_HANDLES)
			h = (java_handle_t *) &(lrt->refs[i]);
#else
			h = (java_handle_t *) o;
#endif

			/*DEBUGLOCALREF("entry add", i);*/

			return h;
		}
	}

	/* this should not happen */

	log_println("localref_add: WARNING: unable to add localref for %p", o);

	return NULL;
}


/* localref_del ****************************************************************

   Deletes an entry from the local reference table.

*******************************************************************************/

void localref_del(java_handle_t *localref)
{
	localref_table *lrt;
	java_handle_t  *h;
	int32_t         localframes;
	int32_t         i;

	/* get local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(localref != NULL);

	localframes = lrt->localframes;

	/* go through all local frames of the current table */
	/* XXX: this is propably not what the spec wants! */

	for (; localframes > 0; localframes--) {

		/* and try to remove the reference */
    
		for (i = 0; i < lrt->capacity; i++) {
#if defined(ENABLE_HANDLES)
			h = (java_handle_t *) &(lrt->refs[i]);
#else
			h = (java_handle_t *) lrt->refs[i];
#endif

			if (h == localref) {
				DEBUGLOCALREF("entry delete", i);

				lrt->refs[i] = NULL;
				lrt->used--;

				return;
			}
		}

		lrt = lrt->prev;
	}

	/* this should not happen */

	log_println("localref_del: WARNING: unable to find localref %p", localref);
}


/* localref_native_enter *******************************************************

   Insert arguments to a native method into the local reference table.
   This is done by the native stub through codegen_start_native_call.

*******************************************************************************/

void localref_native_enter(methodinfo *m, uint64_t *argument_regs, uint64_t *argument_stack)
{
	localref_table *lrt;
	methoddesc     *md;
	imm_union       arg;
	java_handle_t  *h;
	int i;

	/* get local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(m != NULL);

	md = m->parseddesc;

	/* walk through all parameters to the method */

	for (i = 0; i < md->paramcount; ++i) {
		/* load TYPE_ADR parameters ... */

		if (md->paramtypes[i].type == TYPE_ADR) {
			arg = argument_jitarray_load(md, i, argument_regs, argument_stack);

			if (arg.a == NULL)
				continue;

			/* ... and insert them into the table */

			h = localref_add((java_object_t *) arg.a);

#if defined(ENABLE_HANDLES)
			/* update the modified parameter if necesarry */

			arg.a = (void *) h;
			argument_jitarray_store(md, i, argument_regs, argument_stack, arg);
#endif
		}
	}
}


/* localref_native_exit ********************************************************

   Undo the wrapping of the return value of a native method. This is
   done by the native stub through codegen_finish_native_call.

   NOTE: This function is only useful if handles are enabled.

*******************************************************************************/

#if defined(ENABLE_HANDLES)
void localref_native_exit(methodinfo *m, uint64_t *return_regs)
{
	localref_table *lrt;
	methoddesc     *md;
	imm_union       ret;
	java_handle_t  *h;

	/* get local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(m != NULL);

	md = m->parseddesc;

	/* load TYPE_ADR return values ... */

	if (md->returntype.type == TYPE_ADR) {
		ret = argument_jitreturn_load(md, return_regs);

		if (ret.a == NULL)
			return;

		h = (java_handle_t *) ret.a;

		/* update the modified return valie */

		ret.a = (void *) h->heap_object;
		argument_jitreturn_store(md, return_regs, ret);

#if !defined(NDEBUG) && 0
		/* removing the entry from the local reference table is not really
		   necesarry, but gives us warnings if the entry does not exist. */

		localref_del(h);
#endif
	}
}
#endif /* defined(ENABLE_HANDLES) */


/* localref_dump ***************************************************************

   Dumps all local reference tables, including all frames.

*******************************************************************************/

#if !defined(NDEBUG)
# define LOCALREF_DUMP_REFS_PER_LINE 4
void localref_dump()
{
	localref_table *lrt;
	int i, j;

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;

	log_println("--------- Local Reference Tables Dump ---------");

	while (lrt != NULL) {
		log_println("Frame #%d, Used=%d, Capacity=%d, Addr=%p:", lrt->localframes, lrt->used, lrt->capacity, (void *) lrt);

			if (lrt->used != 0) {

				log_start();

				j = 0;
				for (i = 0; i < lrt->capacity; i++) {
					if (lrt->refs[i] != NULL) {
						if (j != 0 && j % LOCALREF_DUMP_REFS_PER_LINE == 0) {
							log_finish();
							log_start();
						}
						j++;
						log_print("\t0x%016lx ", (intptr_t) lrt->refs[i]);
					}
				}

				log_finish();
			}

		lrt = lrt->prev;
	}
}
#endif /* !defined(NDEBUG) */


/* localref_check_uncleared ****************************************************

   Checks the topmost local reference table for uncleared references.

*******************************************************************************/

#if !defined(NDEBUG)
static bool localref_check_uncleared()
{
	localref_table *lrt;
	int32_t         localframes;
	int32_t         lrt_uncleared;
	int32_t         lrt_used;
	int i;

	/* get current local reference table from thread */

	lrt = LOCALREFTABLE;
	assert(lrt != NULL);
	assert(lrt->localframes > 0);

	localframes   = lrt->localframes;
	lrt_uncleared = 0;
	lrt_used      = 0;

	for (; localframes > 0; localframes--) {
		lrt_used += lrt->used;

		for (i = 0; i < lrt->capacity; i++) {
			if (lrt->refs[i] != NULL)
				lrt_uncleared++;
		}

		lrt = lrt->prev;
	}

	if (lrt_uncleared != lrt_used) {
		localref_dump();
		vm_abort("localref_check_uncleared: (uncleared=%d) != (used=%d)", lrt_uncleared, lrt_used);
	}

	if (lrt_uncleared <= 1)
		return true;
	else {
		/*log_println("localref_check_uncleared: %d uncleared local references", lrt_uncleared);*/
		return false;
	}
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
 * vim:noexpandtab:sw=4:ts=4:
 */
