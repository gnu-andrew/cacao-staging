/* src/vm/jit/builtin.cpp - functions for unsupported operations

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

   Contains C functions for JavaVM Instructions that cannot be
   translated to machine language directly. Consequently, the
   generated machine code for these instructions contains function
   calls instead of machine instructions, using the C calling
   convention.

*/


#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "vm/types.h"

#include "arch.h"
#include "md-abi.h"

#include "fdlibm/fdlibm.h"
#if defined(__CYGWIN__) && defined(Bias)
# undef Bias
#endif

#include "mm/gc.hpp"
#include "mm/memory.h"

#include "native/llni.h"

#include "threads/lock-common.h"
#include "threads/mutex.hpp"
#include "threads/thread.hpp"

#include "toolbox/logging.h"
#include "toolbox/util.h"

#include "vm/array.h"
#include "vm/jit/builtin.hpp"
#include "vm/class.h"
#include "vm/cycles-stats.h"
#include "vm/exceptions.hpp"
#include "vm/global.h"
#include "vm/globals.hpp"
#include "vm/initialize.h"
#include "vm/linker.h"
#include "vm/loader.hpp"
#include "vm/options.h"
#include "vm/primitive.hpp"
#include "vm/rt-timing.h"
#include "vm/string.hpp"

#include "vm/jit/asmpart.h"
#include "vm/jit/stubs.hpp"
#include "vm/jit/trace.hpp"

#if defined(ENABLE_VMLOG)
#include <vmlog_cacao.h>
#endif


/* include builtin tables *****************************************************/

#include "vm/jit/builtintable.inc"


CYCLES_STATS_DECLARE(builtin_new         ,100,5)
CYCLES_STATS_DECLARE(builtin_overhead    , 80,1)


/*============================================================================*/
/* BUILTIN TABLE MANAGEMENT FUNCTIONS                                         */
/*============================================================================*/

/* builtintable_init ***********************************************************

   Parse the descriptors of builtin functions and create the parsed
   descriptors.

*******************************************************************************/

static bool builtintable_init(void)
{
	descriptor_pool    *descpool;
	builtintable_entry *bte;
	methodinfo         *m;

	// Create new dump memory area.
	DumpMemoryArea dma;

	/* create a new descriptor pool */

	descpool = descriptor_pool_new(class_java_lang_Object);

	/* add some entries we need */

	if (!descriptor_pool_add_class(descpool, utf_java_lang_Object))
		return false;

	if (!descriptor_pool_add_class(descpool, utf_java_lang_Class))
		return false;

	/* first add all descriptors to the pool */

	for (bte = builtintable_internal; bte->fp != NULL; bte++) {
		bte->name       = utf_new_char(bte->cname);
		bte->descriptor = utf_new_char(bte->cdescriptor);

		if (!descriptor_pool_add(descpool, bte->descriptor, NULL))
			return false;
	}

	for (bte = builtintable_automatic; bte->fp != NULL; bte++) {
		bte->descriptor = utf_new_char(bte->cdescriptor);

		if (!descriptor_pool_add(descpool, bte->descriptor, NULL))
			return false;
	}

	for (bte = builtintable_function; bte->fp != NULL; bte++) {
		bte->classname  = utf_new_char(bte->cclassname);
		bte->name       = utf_new_char(bte->cname);
		bte->descriptor = utf_new_char(bte->cdescriptor);

		if (!descriptor_pool_add(descpool, bte->descriptor, NULL))
			return false;
	}

	/* create the class reference table */

	(void) descriptor_pool_create_classrefs(descpool, NULL);

	/* allocate space for the parsed descriptors */

	descriptor_pool_alloc_parsed_descriptors(descpool);

	/* Now parse all descriptors.  NOTE: builtin-functions are treated
	   like static methods (no `this' pointer). */

	for (bte = builtintable_internal; bte->fp != NULL; bte++) {
		bte->md =
			descriptor_pool_parse_method_descriptor(descpool,
													bte->descriptor,
													ACC_STATIC | ACC_METHOD_BUILTIN,
													NULL);

		/* generate a builtin stub if we need one */

		if (bte->flags & BUILTINTABLE_FLAG_STUB) {
			m = method_new_builtin(bte);
			BuiltinStub::generate(m, bte);
		}
	}

	for (bte = builtintable_automatic; bte->fp != NULL; bte++) {
		bte->md =
			descriptor_pool_parse_method_descriptor(descpool,
													bte->descriptor,
													ACC_STATIC | ACC_METHOD_BUILTIN,
													NULL);

		/* no stubs should be needed for this table */

		assert(!bte->flags & BUILTINTABLE_FLAG_STUB);
	}

	for (bte = builtintable_function; bte->fp != NULL; bte++) {
		bte->md =
			descriptor_pool_parse_method_descriptor(descpool,
													bte->descriptor,
													ACC_STATIC | ACC_METHOD_BUILTIN,
													NULL);

		/* generate a builtin stub if we need one */

		if (bte->flags & BUILTINTABLE_FLAG_STUB) {
			m = method_new_builtin(bte);
			BuiltinStub::generate(m, bte);
		}
	}

	return true;
}


/* builtintable_comparator *****************************************************

   qsort comparator for the automatic builtin table.

*******************************************************************************/

static int builtintable_comparator(const void *a, const void *b)
{
	builtintable_entry *bte1;
	builtintable_entry *bte2;

	bte1 = (builtintable_entry *) a;
	bte2 = (builtintable_entry *) b;

	return (bte1->opcode < bte2->opcode) ? -1 : (bte1->opcode > bte2->opcode);
}


/* builtintable_sort_automatic *************************************************

   Sorts the automatic builtin table.

*******************************************************************************/

static void builtintable_sort_automatic(void)
{
	s4 entries;

	/* calculate table size statically (`- 1' comment see builtintable.inc) */

	entries = sizeof(builtintable_automatic) / sizeof(builtintable_entry) - 1;

	qsort(builtintable_automatic, entries, sizeof(builtintable_entry),
		  builtintable_comparator);
}


/* builtin_init ****************************************************************

   Initialize the global table of builtin functions.

*******************************************************************************/

bool builtin_init(void)
{
	TRACESUBSYSTEMINITIALIZATION("builtin_init");

	/* initialize the builtin tables */

	if (!builtintable_init())
		return false;

	/* sort builtin tables */

	builtintable_sort_automatic();

	return true;
}


/* builtintable_get_internal ***************************************************

   Finds an entry in the builtintable for internal functions and
   returns the a pointer to the structure.

*******************************************************************************/

builtintable_entry *builtintable_get_internal(functionptr fp)
{
	builtintable_entry *bte;

	for (bte = builtintable_internal; bte->fp != NULL; bte++) {
		if (bte->fp == fp)
			return bte;
	}

	return NULL;
}


/* builtintable_get_automatic **************************************************

   Finds an entry in the builtintable for functions which are replaced
   automatically and returns the a pointer to the structure.

*******************************************************************************/

builtintable_entry *builtintable_get_automatic(s4 opcode)
{
	builtintable_entry *first;
	builtintable_entry *last;
	builtintable_entry *middle;
	s4                  half;
	s4                  entries;

	/* calculate table size statically (`- 1' comment see builtintable.inc) */

	entries = sizeof(builtintable_automatic) / sizeof(builtintable_entry) - 1;

	first = builtintable_automatic;
	last = builtintable_automatic + entries;

	while (entries > 0) {
		half = entries / 2;
		middle = first + half;

		if (middle->opcode < opcode) {
			first = middle + 1;
			entries -= half + 1;
		}
		else
			entries = half;
	}

	return (first != last ? first : NULL);
}


/* builtintable_replace_function ***********************************************

   XXX

*******************************************************************************/

#if defined(ENABLE_JIT)
bool builtintable_replace_function(void *iptr_)
{
	constant_FMIref    *mr;
	builtintable_entry *bte;
	instruction        *iptr;

	iptr = (instruction *) iptr_; /* twisti will kill me ;) */

	/* get name and descriptor of the function */

	switch (iptr->opc) {
	case ICMD_INVOKESTATIC:
		/* The instruction MUST be resolved, otherwise we run into
		   lazy loading troubles.  Anyway, we should/can only replace
		   very VM-close functions. */

		if (INSTRUCTION_IS_UNRESOLVED(iptr))
			return false;

		mr = iptr->sx.s23.s3.fmiref;
		break;	

	default:
		return false;
	}

	/* search the function table */

	for (bte = builtintable_function; bte->fp != NULL; bte++) {
		if ((METHODREF_CLASSNAME(mr) == bte->classname) &&
			(mr->name                == bte->name) &&
			(mr->descriptor          == bte->descriptor)) {

			/* set the values in the instruction */

			iptr->opc           = bte->opcode;
			iptr->sx.s23.s3.bte = bte;

			if (bte->flags & BUILTINTABLE_FLAG_EXCEPTION)
				iptr->flags.bits |= INS_FLAG_CHECK;
			else
				iptr->flags.bits &= ~INS_FLAG_CHECK;

			return true;
		}
	}

	return false;
}
#endif /* defined(ENABLE_JIT) */


/*============================================================================*/
/* INTERNAL BUILTIN FUNCTIONS                                                 */
/*============================================================================*/

/* builtin_instanceof **********************************************************

   Checks if an object is an instance of some given class (or subclass
   of that class). If class is an interface, checks if the interface
   is implemented.

   RETURN VALUE:
     1......o is an instance of class or implements the interface
     0......otherwise or if o == NULL

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

bool builtin_instanceof(java_handle_t *o, classinfo *c)
{
	classinfo *oc;

	if (o == NULL)
		return 0;

	LLNI_class_get(o, oc);

	return class_isanysubclass(oc, c);
}



/* builtin_checkcast ***********************************************************

   The same as builtin_instanceof but with the exception
   that 1 is returned when (o == NULL).

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

bool builtin_checkcast(java_handle_t *o, classinfo *c)
{
	classinfo *oc;

	if (o == NULL)
		return 1;

	LLNI_class_get(o, oc);

	if (class_isanysubclass(oc, c))
		return 1;

	return 0;
}


/* builtin_descriptorscompatible ***********************************************

   Checks if two array type descriptors are assignment compatible.

   RETURN VALUE:
      1......target = desc is possible
      0......otherwise
			
*******************************************************************************/

static bool builtin_descriptorscompatible(arraydescriptor *desc, arraydescriptor *target)
{
	if (desc == target)
		return 1;

	if (desc->arraytype != target->arraytype)
		return 0;

	if (desc->arraytype != ARRAYTYPE_OBJECT)
		return 1;
	
	/* {both arrays are arrays of references} */

	if (desc->dimension == target->dimension) {
		if (!desc->elementvftbl)
			return 0;
		/* an array which contains elements of interface types is
           allowed to be casted to Object (JOWENN)*/

		if ((desc->elementvftbl->baseval < 0) &&
			(target->elementvftbl->baseval == 1))
			return 1;

		return class_isanysubclass(desc->elementvftbl->clazz,
								   target->elementvftbl->clazz);
	}

	if (desc->dimension < target->dimension)
		return 0;

	/* {desc has higher dimension than target} */

	return class_isanysubclass(pseudo_class_Arraystub,
							   target->elementvftbl->clazz);
}


/* builtin_arraycheckcast ******************************************************

   Checks if an object is really a subtype of the requested array
   type.  The object has to be an array to begin with. For simple
   arrays (int, short, double, etc.) the types have to match exactly.
   For arrays of objects, the type of elements in the array has to be
   a subtype (or the same type) of the requested element type. For
   arrays of arrays (which in turn can again be arrays of arrays), the
   types at the lowest level have to satisfy the corresponding sub
   class relation.

   NOTE: This is a FAST builtin and can be called from JIT code only.

*******************************************************************************/

bool builtin_fast_arraycheckcast(java_object_t *o, classinfo *targetclass)
{
	arraydescriptor *desc;

	if (o == NULL)
		return 1;

	desc = o->vftbl->arraydesc;

	if (desc == NULL)
		return 0;
 
	return builtin_descriptorscompatible(desc, targetclass->vftbl->arraydesc);
}


/* builtin_fast_arrayinstanceof ************************************************

   NOTE: This is a FAST builtin and can be called from JIT code only.

*******************************************************************************/

bool builtin_fast_arrayinstanceof(java_object_t *o, classinfo *targetclass)
{
	if (o == NULL)
		return 0;

	return builtin_fast_arraycheckcast(o, targetclass);
}


/* builtin_arrayinstanceof *****************************************************

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

bool builtin_arrayinstanceof(java_handle_t *h, classinfo *targetclass)
{
	bool result;

	LLNI_CRITICAL_START;

	result = builtin_fast_arrayinstanceof(LLNI_UNWRAP(h), targetclass);

	LLNI_CRITICAL_END;

	return result;
}


/* builtin_throw_exception *****************************************************

   Sets the exception pointer with the thrown exception and prints some
   debugging information.
   
   NOTE: This is a FAST builtin and can be called from JIT code,
   or from asm_vm_call_method.

*******************************************************************************/

void *builtin_throw_exception(java_object_t *xptr)
{
#if !defined(NDEBUG)
	/* print exception trace */

	if (opt_TraceExceptions)
		trace_exception_builtin(xptr);
#endif /* !defined(NDEBUG) */

	/* actually set the exception */

	exceptions_set_exception(LLNI_QUICKWRAP(xptr));

	/* Return a NULL pointer.  This is required for vm_call_method to
	   check for an exception.  This is for convenience. */

	return NULL;
}


/* builtin_retrieve_exception **************************************************

   Gets and clears the exception pointer of the current thread.

   RETURN VALUE:
      the exception object, or NULL if no exception was thrown.

   NOTE: This is a FAST builtin and can be called from JIT code,
   or from the signal handlers.

*******************************************************************************/

java_object_t *builtin_retrieve_exception(void)
{
	java_handle_t *h;
	java_object_t *o;

	/* actually get and clear the exception */

	h = exceptions_get_and_clear_exception();
	o = LLNI_UNWRAP(h);

	return o;
}


/* builtin_canstore ************************************************************

   Checks, if an object can be stored in an array.

   RETURN VALUE:
      1......possible
      0......otherwise (throws an ArrayStoreException)

   NOTE: This is a SLOW builtin and can be called from JIT & NATIVE code.

*******************************************************************************/

bool builtin_canstore(java_handle_objectarray_t *oa, java_handle_t *o)
{
	bool result;

	LLNI_CRITICAL_START;

	result = builtin_fast_canstore(LLNI_DIRECT(oa), LLNI_UNWRAP(o));

	LLNI_CRITICAL_END;

	/* if not possible, throw an exception */

	if (result == 0)
		exceptions_throw_arraystoreexception();

	return result;
}


/* builtin_fast_canstore *******************************************************

   Checks, if an object can be stored in an array.

   RETURN VALUE:
      1......possible
      0......otherwise (no exception thrown!)

   NOTE: This is a FAST builtin and can be called from JIT code only.

*******************************************************************************/

bool fast_subtype_check(struct _vftbl *s, struct _vftbl *t)
{
	int i;
	if (s->subtype_display[t->subtype_depth] == t)
		return true;
	if (t->subtype_offset != OFFSET(vftbl_t, subtype_display[DISPLAY_SIZE]))
		return false;
	for (i=0; i<s->subtype_overflow_length; i++)
		if (s->subtype_overflow[i] == t)
			return true;
	return false;
}

bool builtin_fast_canstore(java_objectarray_t *oa, java_object_t *o)
{
	arraydescriptor *desc;
	arraydescriptor *valuedesc;
	vftbl_t         *componentvftbl;
	vftbl_t         *valuevftbl;
	int32_t          baseval;
	uint32_t         diffval;
	bool             result;

	if (o == NULL)
		return 1;

	/* The following is guaranteed (by verifier checks):
	 *
	 *     *) oa->...vftbl->arraydesc != NULL
	 *     *) oa->...vftbl->arraydesc->componentvftbl != NULL
	 *     *) o->vftbl is not an interface vftbl
	 */

	desc           = oa->header.objheader.vftbl->arraydesc;
	componentvftbl = desc->componentvftbl;
	valuevftbl     = o->vftbl;
	valuedesc      = valuevftbl->arraydesc;

	if ((desc->dimension - 1) == 0) {
		/* {oa is a one-dimensional array} */
		/* {oa is an array of references} */
		
		if (valuevftbl == componentvftbl)
			return 1;

		baseval = componentvftbl->baseval;

		if (baseval <= 0) {
			/* an array of interface references */

			result = ((valuevftbl->interfacetablelength > -baseval) &&
					  (valuevftbl->interfacetable[baseval] != NULL));
		}
		else {
			result = fast_subtype_check(valuevftbl, componentvftbl);
		}
	}
	else if (valuedesc == NULL) {
		/* {oa has dimension > 1} */
		/* {componentvftbl->arraydesc != NULL} */

		/* check if o is an array */

		return 0;
	}
	else {
		/* {o is an array} */

		result = builtin_descriptorscompatible(valuedesc, componentvftbl->arraydesc);
	}

	/* return result */

	return result;
}


/* This is an optimized version where a is guaranteed to be one-dimensional */
bool builtin_fast_canstore_onedim(java_objectarray_t *a, java_object_t *o)
{
	arraydescriptor *desc;
	vftbl_t         *elementvftbl;
	vftbl_t         *valuevftbl;
	int32_t          baseval;
	uint32_t         diffval;
	bool             result;
	
	if (o == NULL)
		return 1;

	/* The following is guaranteed (by verifier checks):
	 *
	 *     *) a->...vftbl->arraydesc != NULL
	 *     *) a->...vftbl->arraydesc->elementvftbl != NULL
	 *     *) a->...vftbl->arraydesc->dimension == 1
	 *     *) o->vftbl is not an interface vftbl
	 */

	desc = a->header.objheader.vftbl->arraydesc;
    elementvftbl = desc->elementvftbl;
	valuevftbl = o->vftbl;

	/* {a is a one-dimensional array} */
	
	if (valuevftbl == elementvftbl)
		return 1;

	baseval = elementvftbl->baseval;

	if (baseval <= 0) {
		/* an array of interface references */
		result = ((valuevftbl->interfacetablelength > -baseval) &&
				  (valuevftbl->interfacetable[baseval] != NULL));
	}
	else {
		result = fast_subtype_check(valuevftbl, elementvftbl);
	}

	return result;
}


/* This is an optimized version where a is guaranteed to be a
 * one-dimensional array of a class type */
bool builtin_fast_canstore_onedim_class(java_objectarray_t *a, java_object_t *o)
{
	vftbl_t  *elementvftbl;
	vftbl_t  *valuevftbl;
	uint32_t  diffval;
	bool      result;
	
	if (o == NULL)
		return 1;

	/* The following is guaranteed (by verifier checks):
	 *
	 *     *) a->...vftbl->arraydesc != NULL
	 *     *) a->...vftbl->arraydesc->elementvftbl != NULL
	 *     *) a->...vftbl->arraydesc->elementvftbl is not an interface vftbl
	 *     *) a->...vftbl->arraydesc->dimension == 1
	 *     *) o->vftbl is not an interface vftbl
	 */

    elementvftbl = a->header.objheader.vftbl->arraydesc->elementvftbl;
	valuevftbl = o->vftbl;

	/* {a is a one-dimensional array} */
	
	if (valuevftbl == elementvftbl)
		return 1;

	result = fast_subtype_check(valuevftbl, elementvftbl);

	return result;
}


/* builtin_new *****************************************************************

   Creates a new instance of class c on the heap.

   RETURN VALUE:
      pointer to the object, or NULL if no memory is available

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

java_handle_t *builtin_new(classinfo *c)
{
	java_handle_t *o;
#if defined(ENABLE_RT_TIMING)
	struct timespec time_start, time_end;
#endif
#if defined(ENABLE_CYCLES_STATS)
	u8 cycles_start, cycles_end;
#endif

	RT_TIMING_GET_TIME(time_start);
	CYCLES_STATS_GET(cycles_start);

	/* is the class loaded */

	assert(c->state & CLASS_LOADED);

	/* check if we can instantiate this class */

	if (c->flags & ACC_ABSTRACT) {
		exceptions_throw_instantiationerror(c);
		return NULL;
	}

	/* is the class linked */

	if (!(c->state & CLASS_LINKED))
		if (!link_class(c))
			return NULL;

	if (!(c->state & CLASS_INITIALIZED)) {
#if !defined(NDEBUG)
		if (initverbose)
			log_message_class("Initialize class (from builtin_new): ", c);
#endif

		if (!initialize_class(c))
			return NULL;
	}

	o = (java_handle_t*) heap_alloc(c->instancesize, c->flags & ACC_CLASS_HAS_POINTERS,
									c->finalizer, true);

	if (!o)
		return NULL;

#if !defined(ENABLE_GC_CACAO) && defined(ENABLE_HANDLES)
	/* XXX this is only a dirty hack to make Boehm work with handles */

	o = LLNI_WRAP((java_object_t *) o);
#endif

	LLNI_vftbl_direct(o) = c->vftbl;

#if defined(ENABLE_THREADS)
	lock_init_object_lock(LLNI_DIRECT(o));
#endif

	CYCLES_STATS_GET(cycles_end);
	RT_TIMING_GET_TIME(time_end);

	CYCLES_STATS_COUNT(builtin_new,cycles_end - cycles_start);
	RT_TIMING_TIME_DIFF(time_start, time_end, RT_TIMING_NEW_OBJECT);

	return o;
}

#if defined(ENABLE_ESCAPE_REASON)
java_handle_t *builtin_escape_reason_new(classinfo *c) {
	print_escape_reasons();
	return builtin_java_new(c);
}
#endif

#if defined(ENABLE_TLH)
java_handle_t *builtin_tlh_new(classinfo *c)
{
	java_handle_t *o;
# if defined(ENABLE_RT_TIMING)
	struct timespec time_start, time_end;
# endif
# if defined(ENABLE_CYCLES_STATS)
	u8 cycles_start, cycles_end;
# endif

	RT_TIMING_GET_TIME(time_start);
	CYCLES_STATS_GET(cycles_start);

	/* is the class loaded */

	assert(c->state & CLASS_LOADED);

	/* check if we can instantiate this class */

	if (c->flags & ACC_ABSTRACT) {
		exceptions_throw_instantiationerror(c);
		return NULL;
	}

	/* is the class linked */

	if (!(c->state & CLASS_LINKED))
		if (!link_class(c))
			return NULL;

	if (!(c->state & CLASS_INITIALIZED)) {
# if !defined(NDEBUG)
		if (initverbose)
			log_message_class("Initialize class (from builtin_new): ", c);
# endif

		if (!initialize_class(c))
			return NULL;
	}

	/*
	o = tlh_alloc(&(THREADOBJECT->tlh), c->instancesize);
	*/
	o = NULL;

	if (o == NULL) {
		o = (java_handle_t*) heap_alloc(c->instancesize, c->flags & ACC_CLASS_HAS_POINTERS,
										c->finalizer, true);
	}

	if (!o)
		return NULL;

# if !defined(ENABLE_GC_CACAO) && defined(ENABLE_HANDLES)
	/* XXX this is only a dirty hack to make Boehm work with handles */

	o = LLNI_WRAP((java_object_t *) o);
# endif

	LLNI_vftbl_direct(o) = c->vftbl;

# if defined(ENABLE_THREADS)
	lock_init_object_lock(LLNI_DIRECT(o));
# endif

	CYCLES_STATS_GET(cycles_end);
	RT_TIMING_GET_TIME(time_end);

/*
	CYCLES_STATS_COUNT(builtin_new,cycles_end - cycles_start);
	RT_TIMING_TIME_DIFF(time_start, time_end, RT_TIMING_NEW_OBJECT);
*/

	return o;
}
#endif


/* builtin_java_new ************************************************************

   NOTE: This is a SLOW builtin and can be called from JIT code only.

*******************************************************************************/

java_handle_t *builtin_java_new(java_handle_t *clazz)
{
	return builtin_new(LLNI_classinfo_unwrap(clazz));
}


/* builtin_fast_new ************************************************************

   Creates a new instance of class c on the heap.

   RETURN VALUE:
      pointer to the object, or NULL if no fast return
      is possible for any reason.

   NOTE: This is a FAST builtin and can be called from JIT code only.

*******************************************************************************/

java_object_t *builtin_fast_new(classinfo *c)
{
	java_object_t *o;
#if defined(ENABLE_RT_TIMING)
	struct timespec time_start, time_end;
#endif
#if defined(ENABLE_CYCLES_STATS)
	u8 cycles_start, cycles_end;
#endif

	RT_TIMING_GET_TIME(time_start);
	CYCLES_STATS_GET(cycles_start);

	/* is the class loaded */

	assert(c->state & CLASS_LOADED);

	/* check if we can instantiate this class */

	if (c->flags & ACC_ABSTRACT)
		return NULL;

	/* is the class linked */

	if (!(c->state & CLASS_LINKED))
		return NULL;

	if (!(c->state & CLASS_INITIALIZED))
		return NULL;

	o = (java_handle_t*) heap_alloc(c->instancesize, c->flags & ACC_CLASS_HAS_POINTERS,
									c->finalizer, false);

	if (!o)
		return NULL;

	o->vftbl = c->vftbl;

#if defined(ENABLE_THREADS)
	lock_init_object_lock(o);
#endif

	CYCLES_STATS_GET(cycles_end);
	RT_TIMING_GET_TIME(time_end);

	CYCLES_STATS_COUNT(builtin_new,cycles_end - cycles_start);
	RT_TIMING_TIME_DIFF(time_start, time_end, RT_TIMING_NEW_OBJECT);

	return o;
}


/* builtin_newarray ************************************************************

   Creates an array with the given vftbl on the heap. This function
   takes as class argument an array class.

   RETURN VALUE:
      pointer to the array or NULL if no memory is available

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

java_handle_t *builtin_newarray(int32_t size, classinfo *arrayclass)
{
	arraydescriptor *desc;
	s4               dataoffset;
	s4               componentsize;
	s4               actualsize;
	java_handle_t   *a;
#if defined(ENABLE_RT_TIMING)
	struct timespec time_start, time_end;
#endif

	RT_TIMING_GET_TIME(time_start);

	desc          = arrayclass->vftbl->arraydesc;
	dataoffset    = desc->dataoffset;
	componentsize = desc->componentsize;

	if (size < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	actualsize = dataoffset + size * componentsize;

	/* check for overflow */

	if (((u4) actualsize) < ((u4) size)) {
		exceptions_throw_outofmemoryerror();
		return NULL;
	}

	a = (java_handle_t*) heap_alloc(actualsize, (desc->arraytype == ARRAYTYPE_OBJECT), NULL, true);

	if (a == NULL)
		return NULL;

#if !defined(ENABLE_GC_CACAO) && defined(ENABLE_HANDLES)
	/* XXX this is only a dirty hack to make Boehm work with handles */

	a = LLNI_WRAP((java_object_t *) a);
#endif

	LLNI_vftbl_direct(a) = arrayclass->vftbl;

#if defined(ENABLE_THREADS)
	lock_init_object_lock(LLNI_DIRECT(a));
#endif

	LLNI_array_size(a) = size;

	RT_TIMING_GET_TIME(time_end);
	RT_TIMING_TIME_DIFF(time_start, time_end, RT_TIMING_NEW_ARRAY);

	return a;
}


/* builtin_java_newarray *******************************************************

   NOTE: This is a SLOW builtin and can be called from JIT code only.

*******************************************************************************/

java_handle_t *builtin_java_newarray(int32_t size, java_handle_t *arrayclazz)
{
	return builtin_newarray(size, LLNI_classinfo_unwrap(arrayclazz));
}


/* builtin_anewarray ***********************************************************

   Creates an array of references to the given class type on the heap.

   RETURN VALUE:
      pointer to the array or NULL if no memory is
      available

   NOTE: This builtin can be called from NATIVE code only.

*******************************************************************************/

java_handle_objectarray_t *builtin_anewarray(int32_t size, classinfo *componentclass)
{
	classinfo *arrayclass;
	
	/* is class loaded */

	assert(componentclass->state & CLASS_LOADED);

	/* is class linked */

	if (!(componentclass->state & CLASS_LINKED))
		if (!link_class(componentclass))
			return NULL;

	arrayclass = class_array_of(componentclass, true);

	if (!arrayclass)
		return NULL;

	return (java_handle_objectarray_t *) builtin_newarray(size, arrayclass);
}


/* builtin_newarray_type ****************************************************

   Creates an array of [type]s on the heap.
	
   RETURN VALUE:
      pointer to the array or NULL if no memory is available

   NOTE: This is a SLOW builtin and can be called from JIT & NATIVE code.

*******************************************************************************/

#define BUILTIN_NEWARRAY_TYPE(type, arraytype)                             \
java_handle_##type##array_t *builtin_newarray_##type(int32_t size)              \
{                                                                          \
	return (java_handle_##type##array_t *)                                 \
		builtin_newarray(size, primitivetype_table[arraytype].arrayclass); \
}

BUILTIN_NEWARRAY_TYPE(boolean, ARRAYTYPE_BOOLEAN)
BUILTIN_NEWARRAY_TYPE(byte,    ARRAYTYPE_BYTE)
BUILTIN_NEWARRAY_TYPE(char,    ARRAYTYPE_CHAR)
BUILTIN_NEWARRAY_TYPE(short,   ARRAYTYPE_SHORT)
BUILTIN_NEWARRAY_TYPE(int,     ARRAYTYPE_INT)
BUILTIN_NEWARRAY_TYPE(long,    ARRAYTYPE_LONG)
BUILTIN_NEWARRAY_TYPE(float,   ARRAYTYPE_FLOAT)
BUILTIN_NEWARRAY_TYPE(double,  ARRAYTYPE_DOUBLE)


/* builtin_multianewarray_intern ***********************************************

   Creates a multi-dimensional array on the heap. The dimensions are
   passed in an array of longs.

   ARGUMENTS:
      n.............number of dimensions to create
      arrayclass....the array class
      dims..........array containing the size of each dimension to create

   RETURN VALUE:
      pointer to the array or NULL if no memory is available

******************************************************************************/

static java_handle_t *builtin_multianewarray_intern(int n,
													classinfo *arrayclass,
													long *dims)
{
	s4             size;
	java_handle_t *a;
	classinfo     *componentclass;
	s4             i;

	/* create this dimension */

	size = (s4) dims[0];
	a = builtin_newarray(size, arrayclass);

	if (!a)
		return NULL;

	/* if this is the last dimension return */

	if (!--n)
		return a;

	/* get the class of the components to create */

	componentclass = arrayclass->vftbl->arraydesc->componentvftbl->clazz;

	/* The verifier guarantees that the dimension count is in the range. */

	/* create the component arrays */

	for (i = 0; i < size; i++) {
		java_handle_t *ea =
#if defined(__MIPS__) && (SIZEOF_VOID_P == 4)
			/* we save an s4 to a s8 slot, 8-byte aligned */

			builtin_multianewarray_intern(n, componentclass, dims + 2);
#else
			builtin_multianewarray_intern(n, componentclass, dims + 1);
#endif

		if (!ea)
			return NULL;

		array_objectarray_element_set((java_handle_objectarray_t *) a, i, ea);
	}

	return a;
}


/* builtin_multianewarray ******************************************************

   Wrapper for builtin_multianewarray_intern which checks all
   dimensions before we start allocating.

   NOTE: This is a SLOW builtin and can be called from JIT code only.

******************************************************************************/

java_handle_objectarray_t *builtin_multianewarray(int n,
												  java_handle_t *arrayclazz,
												  long *dims)
{
	classinfo *c;
	s4         i;
	s4         size;

	/* check all dimensions before doing anything */

	for (i = 0; i < n; i++) {
#if defined(__MIPS__) && (SIZEOF_VOID_P == 4)
		/* we save an s4 to a s8 slot, 8-byte aligned */
		size = (s4) dims[i * 2];
#else
		size = (s4) dims[i];
#endif

		if (size < 0) {
			exceptions_throw_negativearraysizeexception();
			return NULL;
		}
	}

	c = LLNI_classinfo_unwrap(arrayclazz);

	/* now call the real function */

	return (java_handle_objectarray_t *)
		builtin_multianewarray_intern(n, c, dims);
}


/* builtin_verbosecall_enter ***************************************************

   Print method call with arguments for -verbose:call.

   XXX: Remove mew once all archs use the new tracer!

*******************************************************************************/

#if !defined(NDEBUG)
#ifdef TRACE_ARGS_NUM
void builtin_verbosecall_enter(s8 a0, s8 a1,
# if TRACE_ARGS_NUM >= 4
							   s8 a2, s8 a3,
# endif
# if TRACE_ARGS_NUM >= 6
							   s8 a4, s8 a5,
# endif
# if TRACE_ARGS_NUM == 8
							   s8 a6, s8 a7,
# endif
							   methodinfo *m)
{
	log_text("builtin_verbosecall_enter: Do not call me anymore!");
}
#endif
#endif /* !defined(NDEBUG) */


/* builtin_verbosecall_exit ****************************************************

   Print method exit for -verbose:call.

   XXX: Remove mew once all archs use the new tracer!

*******************************************************************************/

#if !defined(NDEBUG)
void builtin_verbosecall_exit(s8 l, double d, float f, methodinfo *m)
{
	log_text("builtin_verbosecall_exit: Do not call me anymore!");
}
#endif /* !defined(NDEBUG) */


/*============================================================================*/
/* MISCELLANEOUS MATHEMATICAL HELPER FUNCTIONS                                */
/*============================================================================*/

/*********** Functions for integer divisions *****************************
 
	On some systems (eg. DEC ALPHA), integer division is not supported by the
	CPU. These helper functions implement the missing functionality.

******************************************************************************/

#if !SUPPORT_DIVISION || defined(DISABLE_GC)
s4 builtin_idiv(s4 a, s4 b)
{
	s4 c;

	c = a / b;

	return c;
}

s4 builtin_irem(s4 a, s4 b)
{
	s4 c;

	c = a % b;

	return c;
}
#endif /* !SUPPORT_DIVISION || defined(DISABLE_GC) */


/* functions for long arithmetics **********************************************

   On systems where 64 bit Integers are not supported by the CPU,
   these functions are needed.

******************************************************************************/

#if !(SUPPORT_LONG && SUPPORT_LONG_ADD)
s8 builtin_ladd(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a + b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lsub(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a - b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lneg(s8 a)
{
	s8 c;

#if U8_AVAILABLE
	c = -a;
#else
	c = builtin_i2l(0);
#endif

	return c;
}
#endif /* !(SUPPORT_LONG && SUPPORT_LONG_ADD) */


#if !(SUPPORT_LONG && SUPPORT_LONG_MUL)
s8 builtin_lmul(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a * b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}
#endif /* !(SUPPORT_LONG && SUPPORT_LONG_MUL) */


#if !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV) || defined (DISABLE_GC)
s8 builtin_ldiv(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a / b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lrem(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a % b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}
#endif /* !(SUPPORT_DIVISION && SUPPORT_LONG && SUPPORT_LONG_DIV) */


#if !(SUPPORT_LONG && SUPPORT_LONG_SHIFT)
s8 builtin_lshl(s8 a, s4 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a << (b & 63);
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lshr(s8 a, s4 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a >> (b & 63);
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lushr(s8 a, s4 b)
{
	s8 c;

#if U8_AVAILABLE
	c = ((u8) a) >> (b & 63);
#else
	c = builtin_i2l(0);
#endif

	return c;
}
#endif /* !(SUPPORT_LONG && SUPPORT_LONG_SHIFT) */


#if !(SUPPORT_LONG && SUPPORT_LONG_LOGICAL)
s8 builtin_land(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a & b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lor(s8 a, s8 b)
{
	s8 c;

#if U8_AVAILABLE
	c = a | b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}

s8 builtin_lxor(s8 a, s8 b) 
{
	s8 c;

#if U8_AVAILABLE
	c = a ^ b; 
#else
	c = builtin_i2l(0);
#endif

	return c;
}
#endif /* !(SUPPORT_LONG && SUPPORT_LONG_LOGICAL) */


#if !(SUPPORT_LONG && SUPPORT_LONG_CMP)
s4 builtin_lcmp(s8 a, s8 b)
{ 
#if U8_AVAILABLE
	if (a < b)
		return -1;

	if (a > b)
		return 1;

	return 0;
#else
	return 0;
#endif
}
#endif /* !(SUPPORT_LONG && SUPPORT_LONG_CMP) */


/* functions for unsupported floating instructions ****************************/

/* used to convert FLT_xxx defines into float values */

static inline float intBitsToFloat(s4 i)
{
	imm_union imb;

	imb.i = i;
	return imb.f;
}


/* used to convert DBL_xxx defines into double values */

static inline float longBitsToDouble(s8 l)
{
	imm_union imb;

	imb.l = l;
	return imb.d;
}


#if !SUPPORT_FLOAT
float builtin_fadd(float a, float b)
{
	if (isnanf(a)) return intBitsToFloat(FLT_NAN);
	if (isnanf(b)) return intBitsToFloat(FLT_NAN);
	if (finitef(a)) {
		if (finitef(b))
			return a + b;
		else
			return b;
	}
	else {
		if (finitef(b))
			return a;
		else {
			if (copysignf(1.0, a) == copysignf(1.0, b))
				return a;
			else
				return intBitsToFloat(FLT_NAN);
		}
	}
}


float builtin_fsub(float a, float b)
{
	return builtin_fadd(a, builtin_fneg(b));
}


float builtin_fmul(float a, float b)
{
	if (isnanf(a)) return intBitsToFloat(FLT_NAN);
	if (isnanf(b)) return intBitsToFloat(FLT_NAN);
	if (finitef(a)) {
		if (finitef(b)) return a * b;
		else {
			if (a == 0) return intBitsToFloat(FLT_NAN);
			else return copysignf(b, copysignf(1.0, b)*a);
		}
	}
	else {
		if (finitef(b)) {
			if (b == 0) return intBitsToFloat(FLT_NAN);
			else return copysignf(a, copysignf(1.0, a)*b);
		}
		else {
			return copysignf(a, copysignf(1.0, a)*copysignf(1.0, b));
		}
	}
}


/* builtin_ddiv ****************************************************************

   Implementation as described in VM Spec.

*******************************************************************************/

float builtin_fdiv(float a, float b)
{
	if (finitef(a)) {
		if (finitef(b)) {
			/* If neither value1' nor value2' is NaN, the sign of the result */
			/* is positive if both values have the same sign, negative if the */
			/* values have different signs. */

			return a / b;

		} else {
			if (isnanf(b)) {
				/* If either value1' or value2' is NaN, the result is NaN. */

				return intBitsToFloat(FLT_NAN);

			} else {
				/* Division of a finite value by an infinity results in a */
				/* signed zero, with the sign-producing rule just given. */

				/* is sign equal? */

				if (copysignf(1.0, a) == copysignf(1.0, b))
					return 0.0;
				else
					return -0.0;
			}
		}

	} else {
		if (isnanf(a)) {
			/* If either value1' or value2' is NaN, the result is NaN. */

			return intBitsToFloat(FLT_NAN);

		} else if (finitef(b)) {
			/* Division of an infinity by a finite value results in a signed */
			/* infinity, with the sign-producing rule just given. */

			/* is sign equal? */

			if (copysignf(1.0, a) == copysignf(1.0, b))
				return intBitsToFloat(FLT_POSINF);
			else
				return intBitsToFloat(FLT_NEGINF);

		} else {
			/* Division of an infinity by an infinity results in NaN. */

			return intBitsToFloat(FLT_NAN);
		}
	}
}


float builtin_fneg(float a)
{
	if (isnanf(a)) return a;
	else {
		if (finitef(a)) return -a;
		else return copysignf(a, -copysignf(1.0, a));
	}
}
#endif /* !SUPPORT_FLOAT */


#if !SUPPORT_FLOAT || !SUPPORT_FLOAT_CMP || defined(ENABLE_INTRP)
s4 builtin_fcmpl(float a, float b)
{
	if (isnanf(a))
		return -1;

	if (isnanf(b))
		return -1;

	if (!finitef(a) || !finitef(b)) {
		a = finitef(a) ? 0 : copysignf(1.0,	a);
		b = finitef(b) ? 0 : copysignf(1.0, b);
	}

	if (a > b)
		return 1;

	if (a == b)
		return 0;

	return -1;
}


s4 builtin_fcmpg(float a, float b)
{
	if (isnanf(a)) return 1;
	if (isnanf(b)) return 1;
	if (!finitef(a) || !finitef(b)) {
		a = finitef(a) ? 0 : copysignf(1.0, a);
		b = finitef(b) ? 0 : copysignf(1.0, b);
	}
	if (a > b) return 1;
	if (a == b) return 0;
	return -1;
}
#endif /* !SUPPORT_FLOAT || !SUPPORT_FLOAT_CMP || defined(ENABLE_INTRP) */


float builtin_frem(float a, float b)
{
	return fmodf(a, b);
}


/* functions for unsupported double instructions ******************************/

#if !SUPPORT_DOUBLE
double builtin_dadd(double a, double b)
{
	if (isnan(a)) return longBitsToDouble(DBL_NAN);
	if (isnan(b)) return longBitsToDouble(DBL_NAN);
	if (finite(a)) {
		if (finite(b)) return a + b;
		else return b;
	}
	else {
		if (finite(b)) return a;
		else {
			if (copysign(1.0, a)==copysign(1.0, b)) return a;
			else return longBitsToDouble(DBL_NAN);
		}
	}
}


double builtin_dsub(double a, double b)
{
	return builtin_dadd(a, builtin_dneg(b));
}


double builtin_dmul(double a, double b)
{
	if (isnan(a)) return longBitsToDouble(DBL_NAN);
	if (isnan(b)) return longBitsToDouble(DBL_NAN);
	if (finite(a)) {
		if (finite(b)) return a * b;
		else {
			if (a == 0) return longBitsToDouble(DBL_NAN);
			else return copysign(b, copysign(1.0, b) * a);
		}
	}
	else {
		if (finite(b)) {
			if (b == 0) return longBitsToDouble(DBL_NAN);
			else return copysign(a, copysign(1.0, a) * b);
		}
		else {
			return copysign(a, copysign(1.0, a) * copysign(1.0, b));
		}
	}
}


/* builtin_ddiv ****************************************************************

   Implementation as described in VM Spec.

*******************************************************************************/

double builtin_ddiv(double a, double b)
{
	if (finite(a)) {
		if (finite(b)) {
			/* If neither value1' nor value2' is NaN, the sign of the result */
			/* is positive if both values have the same sign, negative if the */
			/* values have different signs. */

			return a / b;

		} else {
			if (isnan(b)) {
				/* If either value1' or value2' is NaN, the result is NaN. */

				return longBitsToDouble(DBL_NAN);

			} else {
				/* Division of a finite value by an infinity results in a */
				/* signed zero, with the sign-producing rule just given. */

				/* is sign equal? */

				if (copysign(1.0, a) == copysign(1.0, b))
					return 0.0;
				else
					return -0.0;
			}
		}

	} else {
		if (isnan(a)) {
			/* If either value1' or value2' is NaN, the result is NaN. */

			return longBitsToDouble(DBL_NAN);

		} else if (finite(b)) {
			/* Division of an infinity by a finite value results in a signed */
			/* infinity, with the sign-producing rule just given. */

			/* is sign equal? */

			if (copysign(1.0, a) == copysign(1.0, b))
				return longBitsToDouble(DBL_POSINF);
			else
				return longBitsToDouble(DBL_NEGINF);

		} else {
			/* Division of an infinity by an infinity results in NaN. */

			return longBitsToDouble(DBL_NAN);
		}
	}
}


/* builtin_dneg ****************************************************************

   Implemented as described in VM Spec.

*******************************************************************************/

double builtin_dneg(double a)
{
	if (isnan(a)) {
		/* If the operand is NaN, the result is NaN (recall that NaN has no */
		/* sign). */

		return a;

	} else {
		if (finite(a)) {
			/* If the operand is a zero, the result is the zero of opposite */
			/* sign. */

			return -a;

		} else {
			/* If the operand is an infinity, the result is the infinity of */
			/* opposite sign. */

			return copysign(a, -copysign(1.0, a));
		}
	}
}
#endif /* !SUPPORT_DOUBLE */


#if !SUPPORT_DOUBLE || !SUPPORT_DOUBLE_CMP || defined(ENABLE_INTRP)
s4 builtin_dcmpl(double a, double b)
{
	if (isnan(a))
		return -1;

	if (isnan(b))
		return -1;

	if (!finite(a) || !finite(b)) {
		a = finite(a) ? 0 : copysign(1.0, a);
		b = finite(b) ? 0 : copysign(1.0, b);
	}

	if (a > b)
		return 1;

	if (a == b)
		return 0;

	return -1;
}


s4 builtin_dcmpg(double a, double b)
{
	if (isnan(a))
		return 1;

	if (isnan(b))
		return 1;

	if (!finite(a) || !finite(b)) {
		a = finite(a) ? 0 : copysign(1.0, a);
		b = finite(b) ? 0 : copysign(1.0, b);
	}

	if (a > b)
		return 1;

	if (a == b)
		return 0;

	return -1;
}
#endif /* !SUPPORT_DOUBLE || !SUPPORT_DOUBLE_CMP || defined(ENABLE_INTRP) */


double builtin_drem(double a, double b)
{
	return fmod(a, b);
}


/* conversion operations ******************************************************/

#if 0
s8 builtin_i2l(s4 i)
{
#if U8_AVAILABLE
	return i;
#else
	s8 v;
	v.high = 0;
	v.low = i;
	return v;
#endif
}

s4 builtin_l2i(s8 l)
{
#if U8_AVAILABLE
	return (s4) l;
#else
	return l.low;
#endif
}
#endif


#if !(SUPPORT_FLOAT && SUPPORT_I2F)
float builtin_i2f(s4 a)
{
	float f = (float) a;
	return f;
}
#endif /* !(SUPPORT_FLOAT && SUPPORT_I2F) */


#if !(SUPPORT_DOUBLE && SUPPORT_I2D)
double builtin_i2d(s4 a)
{
	double d = (double) a;
	return d;
}
#endif /* !(SUPPORT_DOUBLE && SUPPORT_I2D) */


#if !(SUPPORT_LONG && SUPPORT_FLOAT && SUPPORT_L2F)
float builtin_l2f(s8 a)
{
#if U8_AVAILABLE
	float f = (float) a;
	return f;
#else
	return 0.0;
#endif
}
#endif /* !(SUPPORT_LONG && SUPPORT_FLOAT && SUPPORT_L2F) */


#if !(SUPPORT_LONG && SUPPORT_DOUBLE && SUPPORT_L2D)
double builtin_l2d(s8 a)
{
#if U8_AVAILABLE
	double d = (double) a;
	return d;
#else
	return 0.0;
#endif
}
#endif /* !(SUPPORT_LONG && SUPPORT_DOUBLE && SUPPORT_L2D) */


#if !(SUPPORT_FLOAT && SUPPORT_F2I) || defined(ENABLE_INTRP) || defined(DISABLE_GC)
s4 builtin_f2i(float a) 
{
	s4 i;

	i = builtin_d2i((double) a);

	return i;

	/*	float f;
	
		if (isnanf(a))
		return 0;
		if (finitef(a)) {
		if (a > 2147483647)
		return 2147483647;
		if (a < (-2147483648))
		return (-2147483648);
		return (s4) a;
		}
		f = copysignf((float) 1.0, a);
		if (f > 0)
		return 2147483647;
		return (-2147483648); */
}
#endif /* !(SUPPORT_FLOAT && SUPPORT_F2I) || defined(ENABLE_INTRP) || defined(DISABLE_GC) */


#if !(SUPPORT_FLOAT && SUPPORT_LONG && SUPPORT_F2L) || defined(DISABLE_GC)
s8 builtin_f2l(float a)
{
	s8 l;

	l = builtin_d2l((double) a);

	return l;

	/*	float f;
	
		if (finitef(a)) {
		if (a > 9223372036854775807L)
		return 9223372036854775807L;
		if (a < (-9223372036854775808L))
		return (-9223372036854775808L);
		return (s8) a;
		}
		if (isnanf(a))
		return 0;
		f = copysignf((float) 1.0, a);
		if (f > 0)
		return 9223372036854775807L;
		return (-9223372036854775808L); */
}
#endif /* !(SUPPORT_FLOAT && SUPPORT_LONG && SUPPORT_F2L) */


#if !(SUPPORT_DOUBLE && SUPPORT_D2I) || defined(ENABLE_INTRP) || defined(DISABLE_GC)
s4 builtin_d2i(double a) 
{ 
	double d;
	
	if (finite(a)) {
		if (a >= 2147483647)
			return 2147483647;
		if (a <= (-2147483647-1))
			return (-2147483647-1);
		return (s4) a;
	}
	if (isnan(a))
		return 0;
	d = copysign(1.0, a);
	if (d > 0)
		return 2147483647;
	return (-2147483647-1);
}
#endif /* !(SUPPORT_DOUBLE && SUPPORT_D2I) || defined(ENABLE_INTRP) || defined(DISABLE_GC) */


#if !(SUPPORT_DOUBLE && SUPPORT_LONG && SUPPORT_D2L) || defined(DISABLE_GC)
s8 builtin_d2l(double a)
{
	double d;
	
	if (finite(a)) {
		if (a >= 9223372036854775807LL)
			return 9223372036854775807LL;
		if (a <= (-9223372036854775807LL-1))
			return (-9223372036854775807LL-1);
		return (s8) a;
	}
	if (isnan(a))
		return 0;
	d = copysign(1.0, a);
	if (d > 0)
		return 9223372036854775807LL;
	return (-9223372036854775807LL-1);
}
#endif /* !(SUPPORT_DOUBLE && SUPPORT_LONG && SUPPORT_D2L) */


#if !(SUPPORT_FLOAT && SUPPORT_DOUBLE)
double builtin_f2d(float a)
{
	if (finitef(a)) return (double) a;
	else {
		if (isnanf(a))
			return longBitsToDouble(DBL_NAN);
		else
			return copysign(longBitsToDouble(DBL_POSINF), (double) copysignf(1.0, a) );
	}
}

float builtin_d2f(double a)
{
	if (finite(a))
		return (float) a;
	else {
		if (isnan(a))
			return intBitsToFloat(FLT_NAN);
		else
			return copysignf(intBitsToFloat(FLT_POSINF), (float) copysign(1.0, a));
	}
}
#endif /* !(SUPPORT_FLOAT && SUPPORT_DOUBLE) */


/*============================================================================*/
/* AUTOMATICALLY REPLACED FUNCTIONS                                           */
/*============================================================================*/

/* builtin_arraycopy ***********************************************************

   Builtin for java.lang.System.arraycopy.

   NOTE: This is a SLOW builtin and can be called from JIT & NATIVE code.

*******************************************************************************/

void builtin_arraycopy(java_handle_t *src, s4 srcStart,
					   java_handle_t *dest, s4 destStart, s4 len)
{
	arraydescriptor *sdesc;
	arraydescriptor *ddesc;
	s4               i;

	if ((src == NULL) || (dest == NULL)) {
		exceptions_throw_nullpointerexception();
		return;
	}

	sdesc = LLNI_vftbl_direct(src)->arraydesc;
	ddesc = LLNI_vftbl_direct(dest)->arraydesc;

	if (!sdesc || !ddesc || (sdesc->arraytype != ddesc->arraytype)) {
		exceptions_throw_arraystoreexception();
		return;
	}

	// Check if offsets and length are positive.
	if ((srcStart < 0) || (destStart < 0) || (len < 0)) {
		exceptions_throw_arrayindexoutofboundsexception();
		return;
	}

	// Check if ranges are valid.
	if ((((uint32_t) srcStart  + (uint32_t) len) > (uint32_t) LLNI_array_size(src)) ||
		(((uint32_t) destStart + (uint32_t) len) > (uint32_t) LLNI_array_size(dest))) {
		exceptions_throw_arrayindexoutofboundsexception();
		return;
	}

	// Special case.
	if (len == 0) {
		return;
	}

	if (sdesc->componentvftbl == ddesc->componentvftbl) {
		/* We copy primitive values or references of exactly the same type */

		s4 dataoffset = sdesc->dataoffset;
		s4 componentsize = sdesc->componentsize;

		LLNI_CRITICAL_START;

		MMOVE(((u1 *) LLNI_DIRECT(dest)) + dataoffset + componentsize * destStart,
			  ((u1 *) LLNI_DIRECT(src))  + dataoffset + componentsize * srcStart,
			  u1, (size_t) len * componentsize);

		LLNI_CRITICAL_END;
	}
	else {
		/* We copy references of different type */

		java_handle_objectarray_t *oas = (java_handle_objectarray_t *) src;
		java_handle_objectarray_t *oad = (java_handle_objectarray_t *) dest;
 
		if (destStart <= srcStart) {
			for (i = 0; i < len; i++) {
				java_handle_t *o;

				o = array_objectarray_element_get(oas, srcStart + i);

				if (!builtin_canstore(oad, o))
					return;

				array_objectarray_element_set(oad, destStart + i, o);
			}
		}
		else {
			/* XXX this does not completely obey the specification!
			   If an exception is thrown only the elements above the
			   current index have been copied. The specification
			   requires that only the elements *below* the current
			   index have been copied before the throw. */

			for (i = len - 1; i >= 0; i--) {
				java_handle_t *o;

				o = array_objectarray_element_get(oas, srcStart + i);

				if (!builtin_canstore(oad, o))
					return;

				array_objectarray_element_set(oad, destStart + i, o);
			}
		}
	}
}


/* builtin_nanotime ************************************************************

   Return the current time in nanoseconds.

*******************************************************************************/

s8 builtin_nanotime(void)
{
	struct timeval tv;
	s8             usecs;

	if (gettimeofday(&tv, NULL) == -1)
		vm_abort("gettimeofday failed: %s", strerror(errno));

	usecs = (s8) tv.tv_sec * (1000 * 1000) + (s8) tv.tv_usec;

	return usecs * 1000;
}


/* builtin_currenttimemillis ***************************************************

   Return the current time in milliseconds.

*******************************************************************************/

s8 builtin_currenttimemillis(void)
{
	s8 msecs;

	msecs = builtin_nanotime() / 1000 / 1000;

	return msecs;
}


/* builtin_clone ***************************************************************

   Function for cloning objects or arrays.

   NOTE: This is a SLOW builtin and can be called from JIT & NATIVE code.

*******************************************************************************/

java_handle_t *builtin_clone(void *env, java_handle_t *o)
{
	arraydescriptor *ad;
	u4               size;
	classinfo       *c;
	java_handle_t   *co;                /* cloned object header               */

	/* get the array descriptor */

	ad = LLNI_vftbl_direct(o)->arraydesc;

	/* we are cloning an array */

	if (ad != NULL) {
		size = ad->dataoffset + ad->componentsize * LLNI_array_size(o);
        
		co = (java_handle_t*) heap_alloc(size, (ad->arraytype == ARRAYTYPE_OBJECT), NULL, true);

		if (co == NULL)
			return NULL;

#if !defined(ENABLE_GC_CACAO) && defined(ENABLE_HANDLES)
		/* XXX this is only a dirty hack to make Boehm work with handles */

		co = LLNI_WRAP((java_object_t *) co);
#endif

		LLNI_CRITICAL_START;

		MCOPY(LLNI_DIRECT(co), LLNI_DIRECT(o), u1, size);

#if defined(ENABLE_GC_CACAO)
		heap_init_objectheader(LLNI_DIRECT(co), size);
#endif

#if defined(ENABLE_THREADS)
		lock_init_object_lock(LLNI_DIRECT(co));
#endif

		LLNI_CRITICAL_END;

		return co;
	}
    
    /* we are cloning a non-array */

    if (!builtin_instanceof(o, class_java_lang_Cloneable)) {
        exceptions_throw_clonenotsupportedexception();
        return NULL;
    }

	/* get the class of the object */

	LLNI_class_get(o, c);

	/* create new object */

    co = builtin_new(c);

    if (co == NULL)
        return NULL;

	LLNI_CRITICAL_START;

	MCOPY(LLNI_DIRECT(co), LLNI_DIRECT(o), u1, c->instancesize);

#if defined(ENABLE_GC_CACAO)
	heap_init_objectheader(LLNI_DIRECT(co), c->instancesize);
#endif

#if defined(ENABLE_THREADS)
	lock_init_object_lock(LLNI_DIRECT(co));
#endif

	LLNI_CRITICAL_END;

    return co;
}


#if defined(ENABLE_CYCLES_STATS)
void builtin_print_cycles_stats(FILE *file)
{
	fprintf(file,"builtin cylce count statistics:\n");

	CYCLES_STATS_PRINT_OVERHEAD(builtin_overhead,file);
	CYCLES_STATS_PRINT(builtin_new         ,file);

	fprintf(file,"\n");
}
#endif /* defined(ENABLE_CYCLES_STATS) */


#if defined(ENABLE_VMLOG)
#define NDEBUG
#include <vmlog_cacao.c>
#endif


/*
 * These are local overrides for various environment variables in Emacs.
 * Please do not remove this and leave it at the end of the file, where
 * Emacs will automagically detect them.
 * ---------------------------------------------------------------------
 * Local variables:
 * mode: c++
 * indent-tabs-mode: t
 * c-basic-offset: 4
 * tab-width: 4
 * End:
 * vim:noexpandtab:sw=4:ts=4:
 */
