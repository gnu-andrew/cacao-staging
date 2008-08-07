/* src/native/vm/gnuclasspath/java_security_VMAccessController.cpp

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

#include "native/jni.h"
#include "native/native.h"

#if defined(ENABLE_JNI_HEADERS)
# include "native/vm/include/java_security_VMAccessController.h"
#endif

#include "vm/global.h"

#include "vm/jit/stacktrace.hpp"

#include "vmcore/utf8.h"


// Native functions are exported as C functions.
extern "C" {

/*
 * Class:     java/security/VMAccessController
 * Method:    getStack
 * Signature: ()[[Ljava/lang/Object;
 */
JNIEXPORT jobjectArray JNICALL Java_java_security_VMAccessController_getStack(JNIEnv *env, jclass clazz)
{
	return (jobjectArray) stacktrace_get_stack();
}

} // extern "C"


/* native methods implemented by this file ************************************/

static JNINativeMethod methods[] = {
	{ (char*) "getStack", (char*) "()[[Ljava/lang/Object;", (void*) (uintptr_t) &Java_java_security_VMAccessController_getStack },
};


/* _Jv_java_security_VMAccessController_init ***********************************

   Register native functions.

*******************************************************************************/

// FIXME
extern "C" {
void _Jv_java_security_VMAccessController_init(void)
{
	utf *u;

	u = utf_new_char("java/security/VMAccessController");

	native_method_register(u, methods, NATIVE_METHODS_COUNT);
}
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
 */
