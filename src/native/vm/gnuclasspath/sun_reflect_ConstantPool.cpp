/* src/native/vm/gnuclasspath/sun_reflect_ConstantPool.cpp

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

/*******************************************************************************

   XXX: The Methods in this file are very redundant to thouse in
        src/native/vm/sun/jvm.c Unless someone has a good idea how to cover
        such redundancy I leave it how it is.

  The ConstantPool class represents an interface to the constant pool of a
  class and is used by the annotations parser (sun.reflect.annotation.
  AnnotationParser) to get the values of the constants refered by the
  annotations.

*******************************************************************************/

#include "config.h"

#include <assert.h>
#include <stdint.h>

#include "mm/memory.h"

#include "native/jni.h"
#include "native/llni.h"
#include "native/native.h"

#include "native/include/java_lang_Object.h"
#include "native/include/java_lang_Class.h"

// FIXME
extern "C" {
#include "native/include/sun_reflect_ConstantPool.h"
}

#include "native/vm/reflect.h"

#include "toolbox/logging.h"

#include "vm/vm.hpp"
#include "vm/exceptions.h"
#include "vm/resolve.h"
#include "vm/stringlocal.h"

#include "vmcore/class.h"
#include "vmcore/utf8.h"


/* native methods implemented by this file ************************************/

static JNINativeMethod methods[] = {
	{ (char*) "getSize0",             (char*) "(Ljava/lang/Object;I)I",                          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getSize0             },
	{ (char*) "getClassAt0",          (char*) "(Ljava/lang/Object;I)Ljava/lang/Class;",          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getClassAt0          },
	{ (char*) "getClassAtIfLoaded0",  (char*) "(Ljava/lang/Object;I)Ljava/lang/Class;",          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getClassAtIfLoaded0  },
	{ (char*) "getMethodAt0",         (char*) "(Ljava/lang/Object;I)Ljava/lang/reflect/Member;", (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getMethodAt0         },
	{ (char*) "getMethodAtIfLoaded0", (char*) "(Ljava/lang/Object;I)Ljava/lang/reflect/Member;", (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getMethodAtIfLoaded0 },
	{ (char*) "getFieldAt0",          (char*) "(Ljava/lang/Object;I)Ljava/lang/reflect/Field;",  (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getFieldAt0          },
	{ (char*) "getFieldAtIfLoaded0",  (char*) "(Ljava/lang/Object;I)Ljava/lang/reflect/Field;",  (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getFieldAtIfLoaded0  },
	{ (char*) "getMemberRefInfoAt0",  (char*) "(Ljava/lang/Object;I)[Ljava/lang/String;",        (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getMemberRefInfoAt0  },
	{ (char*) "getIntAt0",            (char*) "(Ljava/lang/Object;I)I",                          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getIntAt0            },
	{ (char*) "getLongAt0",           (char*) "(Ljava/lang/Object;I)J",                          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getLongAt0           },
	{ (char*) "getFloatAt0",          (char*) "(Ljava/lang/Object;I)F",                          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getFloatAt0          },
	{ (char*) "getDoubleAt0",         (char*) "(Ljava/lang/Object;I)D",                          (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getDoubleAt0         },
	{ (char*) "getStringAt0",         (char*) "(Ljava/lang/Object;I)Ljava/lang/String;",         (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getStringAt0         },
	{ (char*) "getUTF8At0",           (char*) "(Ljava/lang/Object;I)Ljava/lang/String;",         (void*) (uintptr_t) &Java_sun_reflect_ConstantPool_getUTF8At0           },	
};


/* _Jv_sun_reflect_ConstantPool_init ******************************************

   Register native functions.

*******************************************************************************/

// FIXME
extern "C" {
void _Jv_sun_reflect_ConstantPool_init(void)
{
	native_method_register(utf_new_char("sun/reflect/ConstantPool"), methods, NATIVE_METHODS_COUNT);
}
}


extern "C" {

/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getSize0
 * Signature: (Ljava/lang/Object;)I
 */
JNIEXPORT int32_t JNICALL Java_sun_reflect_ConstantPool_getSize0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool)
{
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);
	return cls->cpcount;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getClassAt0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/Class;
 */
JNIEXPORT struct java_lang_Class* JNICALL Java_sun_reflect_ConstantPool_getClassAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_classref *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_classref*)class_getconstant(
		cls, index, CONSTANT_Class);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	return LLNI_classinfo_wrap(resolve_classref_eager(ref));
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getClassAtIfLoaded0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/Class;
 */
JNIEXPORT struct java_lang_Class* JNICALL Java_sun_reflect_ConstantPool_getClassAtIfLoaded0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_classref *ref;
	classinfo *c = NULL;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_classref*)class_getconstant(
		cls, index, CONSTANT_Class);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}
	
	if (!resolve_classref(NULL,ref,resolveLazy,true,true,&c)) {
		return NULL;
	}

	if (c == NULL || !(c->state & CLASS_LOADED)) {
		return NULL;
	}
	
	return LLNI_classinfo_wrap(c);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getMethodAt0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/reflect/Member;
 */
JNIEXPORT struct java_lang_reflect_Member* JNICALL Java_sun_reflect_ConstantPool_getMethodAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_FMIref *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);
	
	ref = (constant_FMIref*)class_getconstant(
		cls, index, CONSTANT_Methodref);
	
	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	/* XXX: is that right? or do I have to use resolve_method_*? */
	return (struct java_lang_reflect_Member*) reflect_method_new(ref->p.method);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getMethodAtIfLoaded0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/reflect/Member;
 */
JNIEXPORT struct java_lang_reflect_Member* JNICALL Java_sun_reflect_ConstantPool_getMethodAtIfLoaded0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_FMIref *ref;
	classinfo *c = NULL;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_FMIref*)class_getconstant(
		cls, index, CONSTANT_Methodref);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	if (!resolve_classref(NULL,ref->p.classref,resolveLazy,true,true,&c)) {
		return NULL;
	}

	if (c == NULL || !(c->state & CLASS_LOADED)) {
		return NULL;
	}

	return (struct java_lang_reflect_Member*) reflect_method_new(ref->p.method);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getFieldAt0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/reflect/Field;
 */
JNIEXPORT struct java_lang_reflect_Field* JNICALL Java_sun_reflect_ConstantPool_getFieldAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_FMIref *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_FMIref*)class_getconstant(
		cls, index, CONSTANT_Fieldref);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	return (struct java_lang_reflect_Field*) reflect_field_new(ref->p.field);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getFieldAtIfLoaded0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/reflect/Field;
 */
JNIEXPORT struct java_lang_reflect_Field* JNICALL Java_sun_reflect_ConstantPool_getFieldAtIfLoaded0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_FMIref *ref;
	classinfo *c;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_FMIref*)class_getconstant(
		cls, index, CONSTANT_Fieldref);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	if (!resolve_classref(NULL,ref->p.classref,resolveLazy,true,true,&c)) {
		return NULL;
	}

	if (c == NULL || !(c->state & CLASS_LOADED)) {
		return NULL;
	}

	return (struct java_lang_reflect_Field*) reflect_field_new(ref->p.field);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getMemberRefInfoAt0
 * Signature: (Ljava/lang/Object;I)[Ljava/lang/String;
 */
JNIEXPORT java_handle_objectarray_t* JNICALL Java_sun_reflect_ConstantPool_getMemberRefInfoAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	log_println("Java_sun_reflect_ConstantPool_getMemberRefInfoAt0(env=%p, jcpool=%p, index=%d): IMPLEMENT ME!", env, jcpool, index);
	return NULL;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getIntAt0
 * Signature: (Ljava/lang/Object;I)I
 */
JNIEXPORT int32_t JNICALL Java_sun_reflect_ConstantPool_getIntAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_integer *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_integer*)class_getconstant(
		cls, index, CONSTANT_Integer);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return 0;
	}

	return ref->value;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getLongAt0
 * Signature: (Ljava/lang/Object;I)J
 */
JNIEXPORT int64_t JNICALL Java_sun_reflect_ConstantPool_getLongAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_long *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_long*)class_getconstant(
		cls, index, CONSTANT_Long);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return 0;
	}

	return ref->value;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getFloatAt0
 * Signature: (Ljava/lang/Object;I)F
 */
JNIEXPORT float JNICALL Java_sun_reflect_ConstantPool_getFloatAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_float *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_float*)class_getconstant(
		cls, index, CONSTANT_Float);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return 0;
	}

	return ref->value;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getDoubleAt0
 * Signature: (Ljava/lang/Object;I)D
 */
JNIEXPORT double JNICALL Java_sun_reflect_ConstantPool_getDoubleAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	constant_double *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (constant_double*)class_getconstant(
		cls, index, CONSTANT_Double);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return 0;
	}

	return ref->value;
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getStringAt0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/String;
 */
JNIEXPORT struct java_lang_String* JNICALL Java_sun_reflect_ConstantPool_getStringAt0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	utf *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);
	
	ref = (utf*)class_getconstant(cls, index, CONSTANT_String);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	/* XXX: I hope literalstring_new is the right Function. */
	return (java_lang_String*)literalstring_new(ref);
}


/*
 * Class:     sun/reflect/ConstantPool
 * Method:    getUTF8At0
 * Signature: (Ljava/lang/Object;I)Ljava/lang/String;
 */
JNIEXPORT struct java_lang_String* JNICALL Java_sun_reflect_ConstantPool_getUTF8At0(JNIEnv *env, struct sun_reflect_ConstantPool* _this, struct java_lang_Object* jcpool, int32_t index)
{
	utf *ref;
	classinfo *cls = LLNI_classinfo_unwrap(jcpool);

	ref = (utf*)class_getconstant(cls, index, CONSTANT_Utf8);

	if (ref == NULL) {
		exceptions_throw_illegalargumentexception();
		return NULL;
	}

	/* XXX: I hope literalstring_new is the right Function. */
	return (java_lang_String*)literalstring_new(ref);
}

} // extern "C"


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