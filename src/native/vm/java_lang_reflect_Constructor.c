/* src/native/vm/java_lang_reflect_Constructor.c

   Copyright (C) 2007 R. Grafl, A. Krall, C. Kruegel,
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

   $Id: java_lang_reflect_Constructor.c 7976 2007-05-29 12:22:55Z twisti $

*/


#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "vm/types.h"

#include "native/jni.h"
#include "native/native.h"

#if defined(WITH_CLASSPATH_SUN)
# include "native/include/java_lang_String.h"           /* required by j.l.CL */
# include "native/include/java_nio_ByteBuffer.h"        /* required by j.l.CL */
# include "native/include/java_lang_ClassLoader.h"       /* required my j.l.C */
#endif

#include "native/include/java_lang_Object.h"             /* required my j.l.C */
#include "native/include/java_lang_Class.h"
#include "native/include/java_lang_String.h"

#include "native/include/java_lang_reflect_Constructor.h"

#include "native/vm/java_lang_reflect_Constructor.h"

#include "toolbox/logging.h"

#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/access.h"
#include "vm/stringlocal.h"

#include "vmcore/class.h"
#include "vmcore/method.h"


/*
 * Class:     java/lang/reflect/Constructor
 * Method:    getModifiers
 * Signature: ()I
 */
s4 _Jv_java_lang_reflect_Constructor_getModifiers(JNIEnv *env, java_lang_reflect_Constructor *this)
{
	classinfo  *c;
	methodinfo *m;

	c = (classinfo *) (this->clazz);
	m = &(c->methods[this->slot]);

	return m->flags;
}


/*
 * Class:     java/lang/reflect/Constructor
 * Method:    getParameterTypes
 * Signature: ()[Ljava/lang/Class;
 */
java_objectarray *_Jv_java_lang_reflect_Constructor_getParameterTypes(JNIEnv *env, java_lang_reflect_Constructor *this)
{
	classinfo  *c;
	methodinfo *m;

	c = (classinfo *) this->clazz;
	m = &(c->methods[this->slot]);

	return method_get_parametertypearray(m);
}


/*
 * Class:     java/lang/reflect/Constructor
 * Method:    getExceptionTypes
 * Signature: ()[Ljava/lang/Class;
 */
java_objectarray *_Jv_java_lang_reflect_Constructor_getExceptionTypes(JNIEnv *env, java_lang_reflect_Constructor *this)
{
	classinfo  *c;
	methodinfo *m;

	c = (classinfo *) this->clazz;
	m = &(c->methods[this->slot]);

	return method_get_exceptionarray(m);
}


/*
 * Class:     java/lang/reflect/Constructor
 * Method:    newInstance
 * Signature: ([Ljava/lang/Object;)Ljava/lang/Object;
 */
java_lang_Object *_Jv_java_lang_reflect_Constructor_newInstance(JNIEnv *env, java_lang_reflect_Constructor *this, java_objectarray *args)
{
	classinfo         *c;
	methodinfo        *m;
	s4                 override;
	java_objectheader *o;

	c = (classinfo *) this->clazz;
	m = &(c->methods[this->slot]);

	/* check method access */

	/* check if we should bypass security checks (AccessibleObject) */

#if defined(WITH_CLASSPATH_GNU)
	override = this->flag;
#elif defined(WITH_CLASSPATH_SUN)
	override = this->override;
#else
# error unknown classpath configuration
#endif

	if (override == false) {
		if (!access_check_method(m, 1))
			return NULL;
	}

	/* create object */

	o = builtin_new(c);

	if (o == NULL)
		return NULL;
        
	/* call initializer */

	(void) _Jv_jni_invokeNative(m, o, args);

	return (java_lang_Object *) o;
}


/*
 * Class:     java/lang/reflect/Constructor
 * Method:    getSignature
 * Signature: ()Ljava/lang/String;
 */
java_lang_String *_Jv_java_lang_reflect_Constructor_getSignature(JNIEnv *env, java_lang_reflect_Constructor *this)
{
	classinfo         *c;
	methodinfo        *m;
	java_objectheader *o;

	c = (classinfo *) this->clazz;
	m = &(c->methods[this->slot]);

	if (m->signature == NULL)
		return NULL;

	o = javastring_new(m->signature);

	/* in error case o is NULL */

	return (java_lang_String *) o;
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
 */
