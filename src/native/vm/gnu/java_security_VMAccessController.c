/* src/native/vm/gnu/java_security_VMAccessController.c

   Copyright (C) 1996-2005, 2006, 2007 R. Grafl, A. Krall, C. Kruegel,
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

   $Id: java_security_VMAccessController.c 7910 2007-05-16 08:02:52Z twisti $

*/


#include "config.h"
#include "vm/types.h"

#include "native/jni.h"
#include "native/native.h"

#include "native/include/java_security_VMAccessController.h"

#include "vm/builtin.h"

#include "vm/jit/stacktrace.h"

#include "vmcore/class.h"
#include "vmcore/options.h"


/* native methods implemented by this file ************************************/

static JNINativeMethod methods[] = {
	{ "getStack", "()[[Ljava/lang/Object;", (void *) (ptrint) &Java_java_security_VMAccessController_getStack },
};


/* _Jv_java_security_VMAccessController_init ***********************************

   Register native functions.

*******************************************************************************/

void _Jv_java_security_VMAccessController_init(void)
{
	utf *u;

	u = utf_new_char("java/security/VMAccessController");

	native_method_register(u, methods, NATIVE_METHODS_COUNT);
}


/*
 * Class:     java/security/VMAccessController
 * Method:    getStack
 * Signature: ()[[Ljava/lang/Object;
 */
JNIEXPORT java_objectarray* JNICALL Java_java_security_VMAccessController_getStack(JNIEnv *env, jclass clazz)
{
	return stacktrace_getStack();
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
