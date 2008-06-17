/* src/native/vm/gnu/gnu_java_lang_management_VMMemoryMXBeanImpl.c

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

#include <stdint.h>

#include "mm/gc-common.h"

#include "native/jni.h"
#include "native/native.h"

#include "native/include/java_lang_management_MemoryUsage.h"

#include "native/include/gnu_java_lang_management_VMMemoryMXBeanImpl.h"

#include "vm/builtin.h"
#include "vm/global.h"
#include "vm/vm.h"

#include "vmcore/class.h"
#include "vmcore/loader.h"               /* XXX only for load_class_bootstrap */
#include "vmcore/options.h"


/* native methods implemented by this file ************************************/

static JNINativeMethod methods[] = {
	{ "getHeapMemoryUsage",                "()Ljava/lang/management/MemoryUsage;", (void *) (uintptr_t) &Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getHeapMemoryUsage                },
	{ "getNonHeapMemoryUsage",             "()Ljava/lang/management/MemoryUsage;", (void *) (uintptr_t) &Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getNonHeapMemoryUsage             },
	{ "getObjectPendingFinalizationCount", "()I",                                  (void *) (uintptr_t) &Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getObjectPendingFinalizationCount },
	{ "isVerbose",                         "()Z",                                  (void *) (uintptr_t) &Java_gnu_java_lang_management_VMMemoryMXBeanImpl_isVerbose                         },
	{ "setVerbose",                        "(Z)V",                                 (void *) (uintptr_t) &Java_gnu_java_lang_management_VMMemoryMXBeanImpl_setVerbose                        },
};


/* _Jv_gnu_java_lang_management_VMMemoryMXBeanImpl_init ************************

   Register native functions.

*******************************************************************************/

void _Jv_gnu_java_lang_management_VMMemoryMXBeanImpl_init(void)
{
	utf *u;

	u = utf_new_char("gnu/java/lang/management/VMMemoryMXBeanImpl");

	native_method_register(u, methods, NATIVE_METHODS_COUNT);
}


/*
 * Class:     gnu/java/lang/management/VMMemoryMXBeanImpl
 * Method:    getHeapMemoryUsage
 * Signature: ()Ljava/lang/management/MemoryUsage;
 */
JNIEXPORT java_lang_management_MemoryUsage* JNICALL Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getHeapMemoryUsage(JNIEnv *env, jclass clazz)
{
	classinfo                        *class_java_lang_management_MemoryUsage;
	java_handle_t                    *o;
	java_lang_management_MemoryUsage *mu;
	methodinfo                       *m;
	int64_t                           init;
	int64_t                           used;
	int64_t                           commited;
	int64_t                           maximum;

	/* get the class */
	/* XXX optimize me! sometime... */

	if (!(class_java_lang_management_MemoryUsage = load_class_bootstrap(utf_new_char("java/lang/management/MemoryUsage"))))
		return false;

	/* create the object */

	o = builtin_new(class_java_lang_management_MemoryUsage);
	
	if (o == NULL)
		return NULL;

	/* cast the object to a MemoryUsage object (for debugability) */

	mu = (java_lang_management_MemoryUsage *) o;

	/* find initializer */

	m = class_findmethod(class_java_lang_management_MemoryUsage,
						 utf_init, utf_new_char("(JJJJ)V"));
	                      	                      
	/* initializer not found */

	if (m == NULL)
		return NULL;

	/* get values from the VM */
	/* XXX if we ever support more than one VM, change this */

	init     = opt_heapstartsize;
	used     = gc_get_total_bytes();
	commited = gc_get_heap_size();
	maximum  = gc_get_max_heap_size();

	/* call initializer */

	(void) vm_call_method(m, o, init, used, commited, maximum);

	return mu;
}


/*
 * Class:     gnu/java/lang/management/VMMemoryMXBeanImpl
 * Method:    getNonHeapMemoryUsage
 * Signature: ()Ljava/lang/management/MemoryUsage;
 */
JNIEXPORT java_lang_management_MemoryUsage* JNICALL Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getNonHeapMemoryUsage(JNIEnv *env, jclass clazz)
{
	log_println("Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getNonHeapMemoryUsage: IMPLEMENT ME!");

	return NULL;
}


/*
 * Class:     gnu/java/lang/management/VMMemoryMXBeanImpl
 * Method:    getObjectPendingFinalizationCount
 * Signature: ()I
 */
JNIEXPORT int32_t JNICALL Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getObjectPendingFinalizationCount(JNIEnv *env, jclass clazz)
{
	log_println("Java_gnu_java_lang_management_VMMemoryMXBeanImpl_getObjectPendingFinalizationCount: IMPLEMENT ME!");

	return 0;
}


/*
 * Class:     gnu/java/lang/management/VMMemoryMXBeanImpl
 * Method:    isVerbose
 * Signature: ()Z
 */
JNIEXPORT int32_t JNICALL Java_gnu_java_lang_management_VMMemoryMXBeanImpl_isVerbose(JNIEnv *env, jclass clazz)
{
	return _Jv_jvm->Java_gnu_java_lang_management_VMMemoryMXBeanImpl_verbose;
}


/*
 * Class:     gnu/java/lang/management/VMMemoryMXBeanImpl
 * Method:    setVerbose
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_gnu_java_lang_management_VMMemoryMXBeanImpl_setVerbose(JNIEnv *env, jclass clazz, int32_t verbose)
{
	_Jv_jvm->Java_gnu_java_lang_management_VMMemoryMXBeanImpl_verbose = verbose;
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