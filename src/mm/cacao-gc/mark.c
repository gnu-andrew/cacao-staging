/* mm/cacao-gc/mark.c - GC module for marking heap objects

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


#include "config.h"

#include "gc.h"
#include "final.h"
#include "heap.h"
#include "mark.h"
#include "rootset.h"
#include "mm/memory.h"
#include "toolbox/logging.h"
#include "vm/global.h"
#include "vmcore/linker.h"


/* Helper Macros **************************************************************/

#define MARK(o) \
	GCSTAT_COUNT_MAX(gcstat_mark_depth, gcstat_mark_depth_max); \
	mark_recursive(o); \
	GCSTAT_DEC(gcstat_mark_depth);


/* mark_recursive **************************************************************

   Recursively mark all objects (including this) which are referenced.

   TODO, XXX: We need to implement a non-recursive version of this!!!

   IN:
	  o.....heap-object to be marked (either OBJECT or ARRAY)

*******************************************************************************/

void mark_recursive(java_objectheader *o)
{
	vftbl_t           *t;
	classinfo         *c;
	fieldinfo         *f;
	java_objectarray  *oa;
	arraydescriptor   *desc;
	java_objectheader *ref;
	void *start, *end;
	int i;

	/* TODO: this needs cleanup!!! */
	start = heap_region_main->base;
	end = heap_region_main->ptr;

	/* uncollectable objects should never get marked this way */
	/* the reference should point into the heap */
	GC_ASSERT(o);
	GC_ASSERT(!GC_TEST_FLAGS(o, HDRFLAG_UNCOLLECTABLE));
	GC_ASSERT(POINTS_INTO(o, start, end));

	/* mark this object */
	GC_SET_MARKED(o);
	GCSTAT_COUNT(gcstat_mark_count);

	/* get the class of this object */
	/* TODO: maybe we do not need this yet, look to move down! */
	t = o->vftbl;
	GC_ASSERT(t);
	c = t->class;
	GC_ASSERT(c);

#if defined(GCCONF_HDRFLAG_REFERENCING)
	/* does this object has pointers? */
	/* TODO: check how often this happens, maybe remove this check! */
	if (!GC_TEST_FLAGS(o, HDRFLAG_REFERENCING))
		return;
#endif

	/* check if we are marking an array */
	if ((desc = t->arraydesc) != NULL) {
		/* this is an ARRAY */

		/* check if the array contains references */
		if (desc->arraytype != ARRAYTYPE_OBJECT)
			return;

		/* for object-arrays we need to check every entry */
		oa = (java_objectarray *) o;
		for (i = 0; i < oa->header.size; i++) {

			/* load the reference value */
			ref = (java_objectheader *) (oa->data[i]);

			/* check for outside or null pointers */
			if (!POINTS_INTO(ref, start, end))
				continue;

			GC_LOG2( printf("Found (%p) from Array\n", (void *) ref); );

			/* do the recursive marking */
			if (!GC_IS_MARKED(ref)) {
				GCSTAT_COUNT_MAX(gcstat_mark_depth, gcstat_mark_depth_max);
				mark_recursive(ref);
				GCSTAT_DEC(gcstat_mark_depth);
			}

		}

	} else {
		/* this is an OBJECT */

		/* for objects we need to check all (non-static) fields */
		for (; c; c = c->super.cls) {
		for (i = 0; i < c->fieldscount; i++) {
			f = &(c->fields[i]);

			/* check if this field contains a non-static reference */
			if (!IS_ADR_TYPE(f->type) || (f->flags & ACC_STATIC))
				continue;

			/* load the reference value */
			ref = *( (java_objectheader **) ((s1 *) o + f->offset) );

			/* check for outside or null pointers */
			if (!POINTS_INTO(ref, start, end))
				continue;

			GC_LOG2( printf("Found (%p) from Field ", (void *) ref);
					field_print(f); printf("\n"); );

			/* do the recursive marking */
			if (!GC_IS_MARKED(ref)) {
				GCSTAT_COUNT_MAX(gcstat_mark_depth, gcstat_mark_depth_max);
				mark_recursive(ref);
				GCSTAT_DEC(gcstat_mark_depth);
			}

		}
		}

	}

}


/* mark_classes ****************************************************************

   Marks all the references from classinfo structures (static fields)

   IN:
      start.....Region to be marked starts here
      end.......Region to be marked ends here 

*******************************************************************************/

void mark_classes(void *start, void *end)
{
	java_objectheader *ref;
	classinfo         *c;
	fieldinfo         *f;
	void *sys_start, *sys_end;
	int i;

	GC_LOG( dolog("GC: Marking from classes ..."); );

	/* TODO: cleanup!!! */
	sys_start = heap_region_sys->base;
	sys_end = heap_region_sys->ptr;

	/* walk through all classinfo blocks */
	for (c = sys_start; c < (classinfo *) sys_end; c++) {

		/* walk through all fields */
		f = c->fields;
		for (i = 0; i < c->fieldscount; i++, f++) {

			/* check if this is a static reference */
			if (!IS_ADR_TYPE(f->type) || !(f->flags & ACC_STATIC))
				continue;

			/* load the reference */
			ref = (java_objectheader *) (f->value.a);

			/* check for outside or null pointers */
			if (!POINTS_INTO(ref, start, end))
				continue;

			/* mark the reference */
			MARK(ref);

		}

	}

}


/* mark_me *********************************************************************

   Marks all Heap Objects which are reachable from a given root-set.

   REMEMBER: Assumes all threads are stopped!

   IN:
	  rs.....root set containing the references

*******************************************************************************/

void mark_me(rootset_t *rs)
{
	java_objectheader *ref;
	final_entry       *fe;
	u4                 f_type;
	void *start, *end;
	int i;

	/* TODO: this needs cleanup!!! */
	start = heap_region_main->base;
	end = heap_region_main->ptr;

	GCSTAT_INIT(gcstat_mark_count);
	GCSTAT_INIT(gcstat_mark_depth);
	GCSTAT_INIT(gcstat_mark_depth_max);

	/* recursively mark all references from classes */
	/*mark_classes(heap_region_main->base, heap_region_main->ptr);*/

	while (rs) {
		GC_LOG( dolog("GC: Marking from rootset (%d entries) ...", rs->refcount); );

		/* mark all references of the rootset */
		for (i = 0; i < rs->refcount; i++) {

			/* is this a marking reference? */
			if (!rs->ref_marks[i])
				continue;

			/* load the reference */
			ref = *( rs->refs[i] );

			/* check for outside or null pointers */
			if (!POINTS_INTO(ref, start, end))
				continue;

			/* do the marking here */
			MARK(ref);

		}

		rs = rs->next;
	}

	/* objects with finalizers will also be marked here. if they have not been
	 * marked before the finalization is triggered */
	/* REMEMBER: all threads are stopped, so we can use unsynced access here */
	fe = list_first_unsynced(final_list);
	while (fe) {
		f_type = fe->type;
		ref    = fe->o;

		/* object not marked, but was reachable before */
		if (f_type == FINAL_REACHABLE && !GC_IS_MARKED(ref)) {
			GC_LOG2( printf("Finalizer triggered for: ");
					heap_print_object(ref); printf("\n"); );

			/* object is now reclaimable */
			fe->type = FINAL_RECLAIMABLE;

			/* keep the object alive until finalizer finishes */
			MARK(ref);

			/* notify the finalizer after collection finished */
			gc_notify_finalizer = true;
		} else

#if 0
		/* object not marked, but was not finalized yet */
		if (f_type == FINAL_RECLAIMABLE && !GC_IS_MARKED(ref)) {
			GC_LOG2( printf("Finalizer not yet started for: ");
					heap_print_object(ref); printf("\n"); );

			/* keep the object alive until finalizer finishes */
			MARK(ref);
		} else

		/* object not marked, but was not finalized yet */
		if (f_type == FINAL_RECLAIMABLE && !GC_IS_MARKED(ref)) {
			GC_LOG2( printf("Finalizer not yet finished for: ");
					heap_print_object(ref); printf("\n"); );

			/* keep the object alive until finalizer finishes */
			MARK(ref);
		} else

		/* object marked, but finalizer already ran */
		/* TODO: nothing has to be done here, remove me! */
		if (f_type == FINAL_FINALIZED && GC_IS_MARKED(ref)) {
			GC_LOG2( printf("Finalizer resurrected object: ");
					heap_print_object(ref); printf("\n"); );

			/* do nothing */
		} else

		/* object not marked, finalizer already ran */
		if (f_type == FINAL_FINALIZED && !GC_IS_MARKED(ref)) {
			GC_LOG2( printf("Finalizer already finished!\n"); );

			/* do nothing */
		} else
#endif

		/* object marked, finalizer not yet run */
		if (f_type == FINAL_REACHABLE && GC_IS_MARKED(ref)) {
			/* do nothing */
		} else

		/* case not yet covered */
		{ assert(0); }

		fe = list_next_unsynced(final_list, fe);
	}

	GC_LOG( dolog("GC: Marking finished."); );

	GC_ASSERT(gcstat_mark_depth == 0);
	GC_ASSERT(gcstat_mark_depth_max > 0);
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
