/* src/vm/primitive.cpp - primitive types

   Copyright (C) 2007, 2008
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

#include "native/jni.h"
#include "native/llni.h"

#include "native/include/java_lang_Boolean.h"
#include "native/include/java_lang_Byte.h"
#include "native/include/java_lang_Short.h"
#include "native/include/java_lang_Character.h"
#include "native/include/java_lang_Integer.h"
#include "native/include/java_lang_Long.h"
#include "native/include/java_lang_Float.h"
#include "native/include/java_lang_Double.h"

#include "vm/builtin.h"
#include "vm/global.h"
#include "vm/primitive.hpp"
#include "vm/vm.hpp"

#include "vmcore/class.h"
#include "vmcore/utf8.h"


/**
 * Returns the primitive class of the given class name.
 *
 * @param name Name of the class.
 *
 * @return Class structure.
 */
classinfo* Primitive::get_class_by_name(utf *name)
{
	int i;

	/* search table of primitive classes */

	for (i = 0; i < PRIMITIVETYPE_COUNT; i++)
		if (primitivetype_table[i].name == name)
			return primitivetype_table[i].class_primitive;

	/* keep compiler happy */

	return NULL;
}


/**
 * Returns the primitive class of the given type.
 *
 * @param type Integer type of the class.
 *
 * @return Class structure.
 */
classinfo* Primitive::get_class_by_type(int type)
{
	return primitivetype_table[type].class_primitive;
}


/**
 * Returns the primitive class of the given type.
 *
 * @param ch 
 *
 * @return Class structure.
 */
classinfo* Primitive::get_class_by_char(char ch)
{
	int index;

	switch (ch) {
	case 'I':
		index = PRIMITIVETYPE_INT;
		break;
	case 'J':
		index = PRIMITIVETYPE_LONG;
		break;
	case 'F':
		index = PRIMITIVETYPE_FLOAT;
		break;
	case 'D':
		index = PRIMITIVETYPE_DOUBLE;
		break;
	case 'B':
		index = PRIMITIVETYPE_BYTE;
		break;
	case 'C':
		index = PRIMITIVETYPE_CHAR;
		break;
	case 'S':
		index = PRIMITIVETYPE_SHORT;
		break;
	case 'Z':
		index = PRIMITIVETYPE_BOOLEAN;
		break;
	case 'V':
		index = PRIMITIVETYPE_VOID;
		break;
	default:
		return NULL;
	}

	return primitivetype_table[index].class_primitive;
}


/**
 * Returns the primitive array-class of the given primitive class
 * name.
 *
 * @param name Name of the class.
 *
 * @return Class structure.
 */
classinfo* Primitive::get_arrayclass_by_name(utf *name)
{
	int i;

	/* search table of primitive classes */

	for (i = 0; i < PRIMITIVETYPE_COUNT; i++)
		if (primitivetype_table[i].name == name)
			return primitivetype_table[i].arrayclass;

	/* keep compiler happy */

	return NULL;
}


/**
 * Returns the primitive array-class of the given type.
 *
 * @param type Integer type of the class.
 *
 * @return Class structure.
 */
classinfo* Primitive::get_arrayclass_by_type(int type)
{
	return primitivetype_table[type].arrayclass;
}


/**
 * Returns the primitive type of the given wrapper-class.
 *
 * @param c Class structure.
 *
 * @return Integer type of the class.
 */
int Primitive::get_type_by_wrapperclass(classinfo *c)
{
	int i;

	/* Search primitive table. */

	for (i = 0; i < PRIMITIVETYPE_COUNT; i++)
		if (primitivetype_table[i].class_wrap == c)
			return i;

	/* Invalid primitive wrapper-class. */

	return -1;
}


/**
 * Box a primitive of the given type.  If the type is an object,
 * simply return it.
 *
 * @param type  Type of the passed value.
 * @param value Value to box.
 *
 * @return Handle of the boxing Java object.
 */
java_handle_t* Primitive::box(int type, imm_union value)
{
	java_handle_t* o;

	switch (type) {
	case PRIMITIVETYPE_BOOLEAN:
		o = box((uint8_t) value.i);
		break;
	case PRIMITIVETYPE_BYTE:
		o = box((int8_t) value.i);
		break;
	case PRIMITIVETYPE_CHAR:
		o = box((uint16_t) value.i);
		break;
	case PRIMITIVETYPE_SHORT:
		o = box((int16_t) value.i);
		break;
	case PRIMITIVETYPE_INT:
		o = box(value.i);
		break;
	case PRIMITIVETYPE_LONG:
		o = box(value.l);
		break;
	case PRIMITIVETYPE_FLOAT:
		o = box(value.f);
		break;
	case PRIMITIVETYPE_DOUBLE:
		o = box(value.d);
		break;
	case PRIMITIVETYPE_VOID:
		o = (java_handle_t*) value.a;
		break;
	default:
		o = NULL;
		vm_abort("primitive_box: invalid primitive type %d", type);
	}

	return o;
}


/**
 * Unbox a primitive of the given type.  If the type is an object,
 * simply return it.
 *
 * @param h Handle of the Java object.
 *
 * @return Unboxed value as union.
 */
imm_union Primitive::unbox(java_handle_t *h)
{
	classinfo *c;
	int        type;
	imm_union  value;

	if (h == NULL) {
		value.a = NULL;
		return value;
	}

	LLNI_class_get(h, c);

	type = get_type_by_wrapperclass(c);

	switch (type) {
	case PRIMITIVETYPE_BOOLEAN:
		value.i = unbox_boolean(h);
		break;
	case PRIMITIVETYPE_BYTE:
		value.i = unbox_byte(h);
		break;
	case PRIMITIVETYPE_CHAR:
		value.i = unbox_char(h);
		break;
	case PRIMITIVETYPE_SHORT:
		value.i = unbox_short(h);
		break;
	case PRIMITIVETYPE_INT:
		value.i = unbox_int(h);
		break;
	case PRIMITIVETYPE_LONG:
		value.l = unbox_long(h);
		break;
	case PRIMITIVETYPE_FLOAT:
		value.f = unbox_float(h);
		break;
	case PRIMITIVETYPE_DOUBLE:
		value.d = unbox_double(h);
		break;
	case -1:
		/* If type is -1 the object is not a primitive box but a
		   normal object. */
		value.a = h;
		break;
	default:
		vm_abort("Primitive::unbox: invalid primitive type %d", type);
	}

	return value;
}


/* primitive_box_xxx ***********************************************************

   Box a primitive type.

*******************************************************************************/

#define PRIMITIVE_BOX_TYPE(name, object, type)	\
java_handle_t* Primitive::box(type value)	\
{                                                   \
	java_handle_t      *o;                          \
	java_lang_##object *jo;                         \
                                                    \
	o = builtin_new(class_java_lang_##object);      \
                                                    \
	if (o == NULL)                                  \
		return NULL;                                \
                                                    \
	jo = (java_lang_##object *) o;                  \
                                                    \
	LLNI_field_set_val(jo, value, value);			\
                                                    \
	return o;                                       \
}

PRIMITIVE_BOX_TYPE(boolean, Boolean,   uint8_t)
PRIMITIVE_BOX_TYPE(byte,    Byte,      int8_t)
PRIMITIVE_BOX_TYPE(char,    Character, uint16_t)
PRIMITIVE_BOX_TYPE(short,   Short,     int16_t)
PRIMITIVE_BOX_TYPE(int,     Integer,   int32_t)
PRIMITIVE_BOX_TYPE(long,    Long,      int64_t)
PRIMITIVE_BOX_TYPE(float,   Float,     float)
PRIMITIVE_BOX_TYPE(double,  Double,    double)


/* primitive_unbox_xxx *********************************************************

   Unbox a primitive type.

*******************************************************************************/

#define PRIMITIVE_UNBOX_TYPE(name, object, type)	\
type Primitive::unbox_##name(java_handle_t *h)	\
{                                                 \
	java_lang_##object *jo;                       \
	type                value;                    \
                                                  \
	jo = (java_lang_##object *) h;                \
                                                  \
	LLNI_field_get_val(jo, value, value);         \
                                                  \
	return value;                                 \
}

PRIMITIVE_UNBOX_TYPE(boolean, Boolean,   uint8_t)
PRIMITIVE_UNBOX_TYPE(byte,    Byte,      int8_t)
PRIMITIVE_UNBOX_TYPE(char,    Character, uint16_t)
PRIMITIVE_UNBOX_TYPE(short,   Short,     int16_t)
PRIMITIVE_UNBOX_TYPE(int,     Integer,   int32_t)
PRIMITIVE_UNBOX_TYPE(long,    Long,      int64_t)
PRIMITIVE_UNBOX_TYPE(float,   Float,     float)
PRIMITIVE_UNBOX_TYPE(double,  Double,    double)


// Legacy C interface.

extern "C" {

	classinfo* Primitive_get_class_by_name(utf *name) { return Primitive::get_class_by_name(name); }
classinfo* Primitive_get_class_by_type(int type) { return Primitive::get_class_by_type(type); }
classinfo* Primitive_get_class_by_char(char ch) { return Primitive::get_class_by_char(ch); }
classinfo* Primitive_get_arrayclass_by_name(utf *name) { return Primitive::get_arrayclass_by_name(name); }
classinfo* Primitive_get_arrayclass_by_type(int type) { return Primitive::get_arrayclass_by_type(type); }
int Primitive_get_type_by_wrapperclass(classinfo *c) { return Primitive::get_type_by_wrapperclass(c); }
java_handle_t* Primitive_box(int type, imm_union value) { return Primitive::box(type, value); }
imm_union Primitive_unbox(java_handle_t *h) { return Primitive::unbox(h); }
}


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