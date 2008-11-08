/* src/vm/annotation.c - class annotations

   Copyright (C) 2006, 2007, 2008
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

#include "native/llni.h"

#include "mm/memory.hpp"

#include "toolbox/logging.hpp"

#include "vm/annotation.h"
#include "vm/array.hpp"
#include "vm/jit/builtin.hpp"
#include "vm/class.hpp"
#include "vm/loader.hpp"
#include "vm/primitive.hpp"
#include "vm/suck.hpp"
#include "vm/types.h"

#if !defined(ENABLE_ANNOTATIONS)
# error annotation support has to be enabled when compling this file!
#endif


/* annotation_bytearrays_resize ***********************************************

   Resize an array of bytearrays.

   IN:
       bytearrays.....array of bytearrays
       size...........new size of the refered array
   
   RETURN VALUE:
       The new array if a resize was neccessarry, the old if the given size
       equals the current size or NULL if an error occured.

*******************************************************************************/

static java_handle_objectarray_t *annotation_bytearrays_resize(
	java_handle_objectarray_t *bytearrays, uint32_t size)
{
	java_handle_objectarray_t *newbas = NULL; /* new array     */
	uint32_t minsize = 0;      /* count of object refs to copy */
	uint32_t oldsize = 0;      /* size of old array            */

	if (bytearrays != NULL) {
		oldsize = array_length_get((java_handle_t*)bytearrays);
		
		/* if the size already fits do nothing */
		if (size == oldsize) {
			return bytearrays;
		}
	}
	
	newbas = builtin_anewarray(size,
		Primitive_get_arrayclass_by_type(PRIMITIVETYPE_BYTE));
	
	/* is there a old byte array array? */
	if (newbas != NULL && bytearrays != NULL) {
		minsize = size < oldsize ? size : oldsize;

		LLNI_CRITICAL_START;
		MCOPY(
			LLNI_array_data(newbas), LLNI_array_data(bytearrays),
			java_object_t*, minsize);
		LLNI_CRITICAL_END;
	}

	return newbas;
}


/* annotation_bytearrays_insert ***********************************************

   Insert a bytearray into an array of bytearrays.

   IN:
       bytearrays........array of bytearrays where 'bytearray' has to be
                         inserted at position 'index'.
       index.............position where 'ba' has to be inserted into
                         'bytearrays'.
       bytearray.........byte array which has to be inserted into
                         'bytearrays'.

   RETURN VALUE:
       The new array if a resize was neccessarry, the old if the given size
       equals the current size or NULL if an error occured.

*******************************************************************************/

static java_handle_t *annotation_bytearrays_insert(
	java_handle_t *bytearrays, uint32_t index,
	java_handle_bytearray_t *bytearray)
{
	java_handle_objectarray_t *bas; /* bytearrays                */
	uint32_t size = 0;              /* current size of the array */

	/* do nothing if NULL is inserted but no array exists */
	if (bytearray == NULL && bytearrays == NULL) {
		return NULL;
	}

	/* get lengths if array exists */
	if (bytearrays != NULL) {
		size = array_length_get(bytearrays);
	}

	bas = (java_handle_objectarray_t*)bytearrays;

	if (bytearray == NULL) {
		/* insert NULL only if array is big enough */
		if (size > index) {
			array_objectarray_element_set(bas, index, NULL);
		}
	}
	else {
		/* resize array if it's not enough for inserted value */
		if (size <= index) {
			bas = annotation_bytearrays_resize(bas, index + 1);

			if (bas == NULL) {
				/* out of memory */
				return NULL;
			}
		}

		array_objectarray_element_set(bas, index, (java_handle_t*)bytearray);
	}
	
	return (java_handle_t*)bas;
}


/* annotation_load_attribute_body *********************************************

   This function loads the body of a generic attribute.

   XXX: Maybe this function should be called loader_load_attribute_body and
        located in vm/loader.c?

   attribute_info {
       u2 attribute_name_index;
       u4 attribute_length;
       u1 info[attribute_length];
   }

   IN:
       cb.................classbuffer from which to read the data.
       errormsg_prefix....prefix for error messages (if any).

   OUT:
       attribute..........bytearray-pointer which will be set to the read data.
   
   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

static bool annotation_load_attribute_body(classbuffer *cb,
	java_handle_bytearray_t **attribute, const char *errormsg_prefix)
{
	uint32_t                 size = 0;    /* size of the attribute     */
	java_handle_bytearray_t *ba   = NULL; /* the raw attributes' bytes */

	assert(cb != NULL);
	assert(attribute != NULL);

	if (!suck_check_classbuffer_size(cb, 4)) {
		log_println("%s: size missing", errormsg_prefix);
		return false;
	}

	/* load attribute_length */
	size = suck_u4(cb);
	
	if (!suck_check_classbuffer_size(cb, size)) {
		log_println("%s: invalid size", errormsg_prefix);
		return false;
	}
	
	/* if attribute_length == 0 then NULL is
	 * the right value for this attribute */
	if (size > 0) {
		ba = builtin_newarray_byte(size);

		if (ba == NULL) {
			/* out of memory */
			return false;
		}

		/* load data */
		LLNI_CRITICAL_START;

		suck_nbytes((uint8_t*)LLNI_array_data(ba), cb, size);

		LLNI_CRITICAL_END;

		/* return data */
		*attribute = ba;
	}
	
	return true;
}


/* annotation_load_method_attribute_annotationdefault *************************

   Load annotation default value.

   AnnotationDefault_attribute {
       u2 attribute_name_index;
       u4 attribute_length;
       element_value default_value;
   }

   IN:
       cb.................classbuffer from which to read the data.
       m..................methodinfo for the method of which the annotation
                          default value is read and into which the value is
                          stored into.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_method_attribute_annotationdefault(
		classbuffer *cb, methodinfo *m)
{
	int                      slot               = 0;
	                         /* the slot of the method                        */
	java_handle_bytearray_t *annotationdefault  = NULL;
	                         /* unparsed annotation defalut value             */
	java_handle_t           *annotationdefaults = NULL;
	                         /* array of unparsed annotation default values   */

	assert(cb != NULL);
	assert(m != NULL);

	LLNI_classinfo_field_get(
		m->clazz, method_annotationdefaults, annotationdefaults);

	if (!annotation_load_attribute_body(
			cb, &annotationdefault,
			"invalid annotation default method attribute")) {
		return false;
	}

	if (annotationdefault != NULL) {
		slot = m - m->clazz->methods;
		annotationdefaults = annotation_bytearrays_insert(
				annotationdefaults, slot, annotationdefault);

		if (annotationdefaults == NULL) {
			return false;
		}

		LLNI_classinfo_field_set(
			m->clazz, method_annotationdefaults, annotationdefaults);
	}

	return true;
}


/* annotation_load_method_attribute_runtimevisibleparameterannotations ********

   Load runtime visible parameter annotations.

   RuntimeVisibleParameterAnnotations_attribute {
       u2 attribute_name_index;
       u4 attribute_length;
       u1 num_parameters;
       {
           u2 num_annotations;
           annotation annotations[num_annotations];
       } parameter_annotations[num_parameters];
   }

   IN:
       cb.................classbuffer from which to read the data.
       m..................methodinfo for the method of which the parameter
                          annotations are read and into which the parameter
                          annotations are stored into.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_method_attribute_runtimevisibleparameterannotations(
		classbuffer *cb, methodinfo *m)
{
	int                      slot                 = 0;
	                         /* the slot of the method                  */
	java_handle_bytearray_t *annotations          = NULL;
	                         /* unparsed parameter annotations          */
	java_handle_t           *parameterannotations = NULL;
	                         /* array of unparsed parameter annotations */

	assert(cb != NULL);
	assert(m != NULL);

	LLNI_classinfo_field_get(
		m->clazz, method_parameterannotations, parameterannotations);

	if (!annotation_load_attribute_body(
			cb, &annotations,
			"invalid runtime visible parameter annotations method attribute")) {
		return false;
	}

	if (annotations != NULL) {
		slot = m - m->clazz->methods;
		parameterannotations = annotation_bytearrays_insert(
				parameterannotations, slot, annotations);

		if (parameterannotations == NULL) {
			return false;
		}

		LLNI_classinfo_field_set(
			m->clazz, method_parameterannotations, parameterannotations);
	}

	return true;
}


/* annotation_load_method_attribute_runtimeinvisibleparameterannotations ******
 
   Load runtime invisible parameter annotations.

   <quote cite="http://jcp.org/en/jsr/detail?id=202">
   The RuntimeInvisibleParameterAnnotations attribute is similar to the
   RuntimeVisibleParameterAnnotations attribute, except that the annotations
   represented by a RuntimeInvisibleParameterAnnotations attribute must not be
   made available for return by reflective APIs, unless the the JVM has
   specifically been instructed to retain these annotations via some
   implementation-specific mechanism such as a command line flag. In the
   absence of such instructions, the JVM ignores this attribute.
   </quote>

   Hotspot loads them into the same bytearray as the runtime visible parameter
   annotations (after the runtime visible parameter annotations). But in J2SE
   the bytearray will only be parsed as if there is only one annotation
   structure in it, so the runtime invisible parameter annotatios will be
   ignored.

   Therefore I do not even bother to read them.

   RuntimeInvisibleParameterAnnotations_attribute {
       u2 attribute_name_index;
       u4 attribute_length;
       u1 num_parameters;
       {
           u2 num_annotations;
           annotation annotations[num_annotations];
       } parameter_annotations[num_parameters];
   }

   IN:
       cb.................classbuffer from which to read the data.
       m..................methodinfo for the method of which the parameter
                          annotations are read and into which the parameter
                          annotations are stored into.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_method_attribute_runtimeinvisibleparameterannotations(
		classbuffer *cb, methodinfo *m)
{
	return loader_skip_attribute_body(cb);
}


/* annotation_load_class_attribute_runtimevisibleannotations ******************
   
   Load runtime visible annotations of a class.
   
   IN:
       cb........the classbuffer from which the attribute has to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_class_attribute_runtimevisibleannotations(
	classbuffer *cb)
{
	java_handle_bytearray_t *annotations = NULL; /* unparsed annotations */
	
	if (!annotation_load_attribute_body(
			cb, &annotations,
			"invalid runtime visible annotations class attribute")) {
		return false;
	}

	LLNI_classinfo_field_set(cb->clazz, annotations, (java_handle_t*)annotations);

	return true;
}


/* annotation_load_class_attribute_runtimeinvisibleannotations ****************
   
   Load runtime invisible annotations of a class (just skip them).
   
   IN:
       cb........the classbuffer from which the attribute has to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_class_attribute_runtimeinvisibleannotations(
	classbuffer *cb)
{
	return loader_skip_attribute_body(cb);
}


/* annotation_load_method_attribute_runtimevisibleannotations *****************
   
   Load runtime visible annotations of a method.
  
   IN:
       cb........the classbuffer from which the attribute has to be loaded.
       m.........the method of which the runtime visible annotations have
                 to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_method_attribute_runtimevisibleannotations(
	classbuffer *cb, methodinfo *m)
{
	int                      slot               = 0;
	                         /* slot of the method */
	java_handle_bytearray_t *annotations        = NULL;
	                         /* unparsed annotations */
	java_handle_t           *method_annotations = NULL;
	                         /* array of unparsed method annotations */

	assert(cb != NULL);
	assert(m != NULL);

	LLNI_classinfo_field_get(
		m->clazz, method_annotations, method_annotations);

	if (!annotation_load_attribute_body(
			cb, &annotations,
			"invalid runtime visible annotations method attribute")) {
		return false;
	}

	if (annotations != NULL) {
		slot = m - m->clazz->methods;
		method_annotations = annotation_bytearrays_insert(
				method_annotations, slot, annotations);

		if (method_annotations == NULL) {
			return false;
		}
		
		LLNI_classinfo_field_set(
			m->clazz, method_annotations, method_annotations);
	}

	return true;
}


/* annotation_load_method_attribute_runtimeinvisibleannotations ****************
   
   Load runtime invisible annotations of a method (just skip them).
   
   IN:
       cb........the classbuffer from which the attribute has to be loaded.
       m.........the method of which the runtime invisible annotations have
                 to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_method_attribute_runtimeinvisibleannotations(
	classbuffer *cb, methodinfo *m)
{
	return loader_skip_attribute_body(cb);
}


/* annotation_load_field_attribute_runtimevisibleannotations ******************
   
   Load runtime visible annotations of a field.
   
   IN:
       cb........the classbuffer from which the attribute has to be loaded.
       f.........the field of which the runtime visible annotations have
                 to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_field_attribute_runtimevisibleannotations(
	classbuffer *cb, fieldinfo *f)
{
	int                      slot              = 0;
	                         /* slot of the field                   */
	java_handle_bytearray_t *annotations       = NULL;
	                         /* unparsed annotations                */
	java_handle_t           *field_annotations = NULL;
	                         /* array of unparsed field annotations */

	assert(cb != NULL);
	assert(f != NULL);

	LLNI_classinfo_field_get(
		f->clazz, field_annotations, field_annotations);

	if (!annotation_load_attribute_body(
			cb, &annotations,
			"invalid runtime visible annotations field attribute")) {
		return false;
	}

	if (annotations != NULL) {
		slot = f - f->clazz->fields;
		field_annotations = annotation_bytearrays_insert(
				field_annotations, slot, annotations);

		if (field_annotations == NULL) {
			return false;
		}

		LLNI_classinfo_field_set(
			f->clazz, field_annotations, field_annotations);
	}

	return true;
}


/* annotation_load_field_attribute_runtimeinvisibleannotations ****************
   
   Load runtime invisible annotations of a field (just skip them).
   
   IN:
       cb........the classbuffer from which the attribute has to be loaded.
       f.........the field of which the runtime invisible annotations have
                 to be loaded.

   RETURN VALUE:
       true if all went good. false otherwhise.

*******************************************************************************/

bool annotation_load_field_attribute_runtimeinvisibleannotations(
	classbuffer *cb, fieldinfo *f)
{
	return loader_skip_attribute_body(cb);
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
