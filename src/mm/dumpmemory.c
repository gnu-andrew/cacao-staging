/* src/mm/dumpmemory.c - dump memory management

   Copyright (C) 1996-2005, 2006, 2007, 2008
   CACAOVM - Verein zu Foerderung der freien virtuellen Machine CACAO

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

#include "mm/dumpmemory.h"
#include "mm/memory.h"

#include "threads/threads-common.h"

#include "vmcore/options.h"

#if defined(ENABLE_STATISTICS)
# include "vmcore/statistics.h"
#endif

#include "vmcore/system.h"

#include "vm/vm.h"


/*******************************************************************************

  This structure is used for dump memory allocation if cacao
  runs without threads.

*******************************************************************************/

#if !defined(ENABLE_THREADS)
static dumpinfo_t _no_threads_dumpinfo;
#endif

#if defined(ENABLE_THREADS)
#define DUMPINFO    &((threadobject *) THREADOBJECT)->dumpinfo
#else
#define DUMPINFO    &_no_threads_dumpinfo
#endif


/* dump_check_canaries *********************************************************

   Check canaries in dump memory.

   IN:
      di...........dumpinfo_t * of the dump area to check
	  bottomsize...dump size down to which the dump area should be checked
	               (specify 0 to check the whole dump area)

   ERROR HANDLING:
      If any canary has been changed, this function aborts the VM with
	  an error message.

*******************************************************************************/

#if defined(ENABLE_MEMCHECK)
static void dump_check_canaries(dumpinfo_t *di, s4 bottomsize)
{
	dump_allocation_t *da;
	uint8_t           *pm;
	int                i, j;

	/* iterate over all dump memory allocations above bottomsize */

	da = di->allocations;

	while (da && da->useddumpsize >= bottomsize) {
		/* check canaries */

		pm = ((uint8_t *) da->mem) - MEMORY_CANARY_SIZE;

		for (i = 0; i < MEMORY_CANARY_SIZE; ++i) {
			if (pm[i] != i + MEMORY_CANARY_FIRST_BYTE) {
				fprintf(stderr, "canary bytes:");

				for (j = 0; j < MEMORY_CANARY_SIZE; ++j)
					fprintf(stderr, " %02x", pm[j]);

				fprintf(stderr,"\n");

				vm_abort("error: dump memory bottom canary killed: "
						 "%p (%d bytes allocated at %p)\n",
						 pm + i, da->size, da->mem);
			}
		}

		pm = ((uint8_t *) da->mem) + da->size;

		for (i = 0; i < MEMORY_CANARY_SIZE; ++i) {
			if (pm[i] != i + MEMORY_CANARY_FIRST_BYTE) {
				fprintf(stderr, "canary bytes:");

				for (j = 0; j < MEMORY_CANARY_SIZE; ++j)
					fprintf(stderr, " %02x", pm[j]);

				fprintf(stderr, "\n");

				vm_abort("error: dump memory top canary killed: "
						 "%p (%d bytes allocated at %p)\n",
						 pm + i, da->size, da->mem);
			}
		}

		da = da->next;
	}
}
#endif /* defined(ENABLE_MEMCHECK) */


/* dump_alloc ******************************************************************

   Allocate memory in the dump area.

   IN:
      size.........size of block to allocate, in bytes
	  			   may be zero, in which case NULL is returned

   RETURN VALUE:
      pointer to allocated memory, or
	  NULL iff `size` was zero

   ERROR HANDLING:
      XXX This function uses `memory_checked_alloc`, which *exits* if no 
	  memory could be allocated.

   THREADS:
      dump_alloc is thread safe. Each thread has its own dump memory area.

   dump_alloc is a fast allocator suitable for scratch memory that can be
   collectively freed when the current activity (eg. compiling) is done.

   You cannot selectively free dump memory. Before you start allocating it, 
   you remember the current size returned by `dump_size`. Later, when you no 
   longer need the memory, call `dump_release` with the remembered size and
   all dump memory allocated since the call to `dump_size` will be freed.

*******************************************************************************/

void *dump_alloc(s4 size)
{
#if defined(DISABLE_DUMP)

	/* use malloc memory for dump memory (for debugging only!) */

	return mem_alloc(size);

#else /* !defined(DISABLE_DUMP) */

	void       *p;
	dumpinfo_t *di;
#if defined(ENABLE_MEMCHECK)
	s4          origsize = size; /* needed for the canary system */
#endif

	/* If no threads are used, the dumpinfo structure is a static structure   */
	/* defined at the top of this file.                                       */

	di = DUMPINFO;

	if (size == 0)
		return NULL;

#if defined(ENABLE_MEMCHECK)
	size += 2*MEMORY_CANARY_SIZE;
#endif

	size = MEMORY_ALIGN(size, ALIGNSIZE);

	if (di->useddumpsize + size > di->allocateddumpsize) {
		dumpblock_t *newdumpblock;
		s4         newdumpblocksize;

		/* allocate a new dumplist structure */

		newdumpblock = memory_checked_alloc(sizeof(dumpblock_t));

		/* If requested size is greater than the default, make the new dump   */
		/* block as big as the size requested. Else use the default size.     */

		if (size > DUMPBLOCKSIZE) {
			newdumpblocksize = size;

		} else {
			newdumpblocksize = DUMPBLOCKSIZE;
		}

		/* allocate dumpblock memory */

		newdumpblock->dumpmem = memory_checked_alloc(newdumpblocksize);

		newdumpblock->prev = di->currentdumpblock;
		newdumpblock->size = newdumpblocksize;
		di->currentdumpblock = newdumpblock;

		/* Used dump size is previously allocated dump size, because the      */
		/* remaining free memory of the previous dump block cannot be used.   */

		di->useddumpsize = di->allocateddumpsize;

		/* increase the allocated dump size by the size of the new dump block */

		di->allocateddumpsize += newdumpblocksize;

#if defined(ENABLE_STATISTICS)
		/* the amount of globally allocated dump memory (thread save) */

		if (opt_stat)
			globalallocateddumpsize += newdumpblocksize;
#endif
	}

	/* current dump block base address + the size of the current dump
	   block - the size of the unused memory = new start address  */

	p = ((uint8_t *) di->currentdumpblock->dumpmem) +
		di->currentdumpblock->size -
		(di->allocateddumpsize - di->useddumpsize);

#if defined(ENABLE_MEMCHECK)
	{
		dump_allocation_t *da = NEW(dump_allocation_t);
		uint8_t           *pm;
		int                i;

		/* add the allocation to our linked list of allocations */

		da->next         = di->allocations;
		da->mem          = ((uint8_t *) p) + MEMORY_CANARY_SIZE;
		da->size         = origsize;
		da->useddumpsize = di->useddumpsize;

		di->allocations = da;

		/* write the canaries */

		pm = (uint8_t *) p;

		for (i = 0; i < MEMORY_CANARY_SIZE; ++i)
			pm[i] = i + MEMORY_CANARY_FIRST_BYTE;

		pm = ((uint8_t *) da->mem) + da->size;

		for (i = 0; i < MEMORY_CANARY_SIZE; ++i)
			pm[i] = i + MEMORY_CANARY_FIRST_BYTE;

		/* make m point after the bottom canary */

		p = ((uint8_t *) p) + MEMORY_CANARY_SIZE;

		/* clear the memory */

		(void) system_memset(p, MEMORY_CLEAR_BYTE, da->size);
	}
#endif /* defined(ENABLE_MEMCHECK) */

	/* increase used dump size by the allocated memory size */

	di->useddumpsize += size;

#if defined(ENABLE_STATISTICS)
	if (opt_stat)
		if (di->useddumpsize > maxdumpsize)
			maxdumpsize = di->useddumpsize;
#endif

	return p;

#endif /* defined(DISABLE_DUMP) */
}


/* dump_realloc ****************************************************************

   Stupid realloc implementation for dump memory. Avoid, if possible.

*******************************************************************************/

void *dump_realloc(void *src, s4 len1, s4 len2)
{
#if defined(DISABLE_DUMP)
	/* use malloc memory for dump memory (for debugging only!) */

	return mem_realloc(src, len1, len2);
#else
	void *dst = dump_alloc(len2);

	(void) system_memcpy(dst, src, len1);

#if defined(ENABLE_MEMCHECK)
	/* destroy the source */
	(void) system_memset(src, MEMORY_CLEAR_BYTE, len1);
#endif

	return dst;
#endif
}


/* dump_release ****************************************************************

   Release dump memory above the given size.

   IN:
       size........All dump memory above this mark will be freed. Usually
	               `size` will be the return value of a `dump_size` call
				   made earlier.

	ERROR HANDLING:
	   XXX If the given size is invalid, this function *exits* with an
	       error message.
				   
	See `dump_alloc`.

*******************************************************************************/

void dump_release(s4 size)
{
#if defined(DISABLE_DUMP)

	/* use malloc memory for dump memory (for debugging only!) */

	/* do nothing */

#else /* !defined(DISABLE_DUMP) */

	dumpinfo_t *di;

	/* If no threads are used, the dumpinfo structure is a static structure   */
	/* defined at the top of this file.                                       */

	di = DUMPINFO;

	if ((size < 0) || (size > di->useddumpsize))
		vm_abort("Illegal dump release size: %d", size);

#if defined(ENABLE_MEMCHECK)
	{
		dump_allocation_t *da, *next;

		/* check canaries */

		dump_check_canaries(di, size);

		/* iterate over all dump memory allocations about to be released */

		da = di->allocations;
		while (da && da->useddumpsize >= size) {
			next = da->next;

			/* invalidate the freed memory */

			(void) system_memset(da->mem, MEMORY_CLEAR_BYTE, da->size);

			FREE(da, dump_allocation_t);

			da = next;
		}
		di->allocations = da;
	}
#endif /* defined(ENABLE_MEMCHECK) */

	/* reset the used dump size to the size specified */

	di->useddumpsize = size;

	while (di->currentdumpblock && di->allocateddumpsize - di->currentdumpblock->size >= di->useddumpsize) {
		dumpblock_t *tmp = di->currentdumpblock;

		di->allocateddumpsize -= tmp->size;
		di->currentdumpblock = tmp->prev;

#if defined(ENABLE_STATISTICS)
		/* the amount of globally allocated dump memory (thread save) */

		if (opt_stat)
			globalallocateddumpsize -= tmp->size;
#endif

		/* release the dump memory and the dumpinfo structure */

		system_free(tmp->dumpmem);
		system_free(tmp);
	}

#endif /* defined(DISABLE_DUMP) */
}


/* dump_size *******************************************************************

   Return the current size of the dump memory area. See `dump_alloc`.

*******************************************************************************/

s4 dump_size(void)
{
#if defined(DISABLE_DUMP)
	/* use malloc memory for dump memory (for debugging only!) */

	return 0;

#else /* !defined(DISABLE_DUMP) */

	dumpinfo_t *di;

	/* If no threads are used, the dumpinfo structure is a static
	   structure defined at the top of this file. */

	di = DUMPINFO;

	if (di == NULL)
		return 0;

	return di->useddumpsize;

#endif /* defined(DISABLE_DUMP) */
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
 * vim:noexpandtab:sw=4:ts=4:
 */
