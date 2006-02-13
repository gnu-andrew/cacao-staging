/* src/native/jni.c - implementation of the Java Native Interface functions

   Copyright (C) 1996-2005, 2006 R. Grafl, A. Krall, C. Kruegel,
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

   Authors: Rainhard Grafl
            Roman Obermaisser

   Changes: Joseph Wenninger
            Martin Platter
            Christian Thalinger

   $Id: jni.c 4501 2006-02-13 18:55:55Z twisti $

*/


#include "config.h"

#include <assert.h>
#include <string.h>

#include "vm/types.h"

#include "mm/boehm.h"
#include "mm/memory.h"
#include "native/jni.h"
#include "native/native.h"

#include "native/include/gnu_classpath_Pointer.h"

#if SIZEOF_VOID_P == 8
# include "native/include/gnu_classpath_Pointer64.h"
#else
# include "native/include/gnu_classpath_Pointer32.h"
#endif

#include "native/include/java_lang_Object.h"
#include "native/include/java_lang_Byte.h"
#include "native/include/java_lang_Character.h"
#include "native/include/java_lang_Short.h"
#include "native/include/java_lang_Integer.h"
#include "native/include/java_lang_Boolean.h"
#include "native/include/java_lang_Long.h"
#include "native/include/java_lang_Float.h"
#include "native/include/java_lang_Double.h"
#include "native/include/java_lang_Throwable.h"
#include "native/include/java_lang_reflect_Method.h"
#include "native/include/java_lang_reflect_Constructor.h"
#include "native/include/java_lang_reflect_Field.h"

#include "native/include/java_lang_Class.h" /* for java_lang_VMClass.h */
#include "native/include/java_lang_VMClass.h"
#include "native/include/java_lang_VMClassLoader.h"
#include "native/include/java_nio_Buffer.h"
#include "native/include/java_nio_DirectByteBufferImpl.h"

#if defined(ENABLE_JVMTI)
# include "native/jvmti/jvmti.h"
#endif

#if defined(USE_THREADS)
# if defined(NATIVE_THREADS)
#  include "threads/native/threads.h"
# else
#  include "threads/green/threads.h"
# endif
#endif

#include "toolbox/logging.h"
#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/global.h"
#include "vm/initialize.h"
#include "vm/loader.h"
#include "vm/options.h"
#include "vm/resolve.h"
#include "vm/statistics.h"
#include "vm/stringlocal.h"
#include "vm/jit/asmpart.h"
#include "vm/jit/jit.h"
#include "vm/statistics.h"


/* XXX TWISTI hack: define it extern so they can be found in this file */

extern const struct JNIInvokeInterface JNI_JavaVMTable;
extern struct JNINativeInterface JNI_JNIEnvTable;

/* pointers to VM and the environment needed by GetJavaVM and GetEnv */

static JavaVM ptr_jvm = (JavaVM) &JNI_JavaVMTable;
void *ptr_env = (void*) &JNI_JNIEnvTable;


#define PTR_TO_ITEM(ptr)   ((u8)(size_t)(ptr))

/* global variables ***********************************************************/

/* global reference table *****************************************************/

static java_objectheader **global_ref_table;

/* jmethodID and jclass caching variables for NewGlobalRef and DeleteGlobalRef*/
static classinfo *ihmclass = NULL;
static methodinfo *putmid = NULL;
static methodinfo *getmid = NULL;
static methodinfo *removemid = NULL;


/* direct buffer stuff ********************************************************/

static classinfo *class_java_nio_Buffer;
static classinfo *class_java_nio_DirectByteBufferImpl;
static classinfo *class_java_nio_DirectByteBufferImpl_ReadWrite;
#if SIZEOF_VOID_P == 8
static classinfo *class_gnu_classpath_Pointer64;
#else
static classinfo *class_gnu_classpath_Pointer32;
#endif

static methodinfo *dbbirw_init;


/* local reference table ******************************************************/

#if !defined(USE_THREADS)
localref_table *_no_threads_localref_table;
#endif


/* accessing instance fields macros *******************************************/

#define SET_FIELD(obj,type,var,value) \
    *((type *) ((ptrint) (obj) + (ptrint) (var)->offset)) = (type) (value)

#define GET_FIELD(obj,type,var) \
    *((type *) ((ptrint) (obj) + (ptrint) (var)->offset))


/* some forward declarations **************************************************/

jobject NewLocalRef(JNIEnv *env, jobject ref);


/* jni_init ********************************************************************

   Initialize the JNI subsystem.

*******************************************************************************/

bool jni_init(void)
{
	/* initalize global reference table */

	if (!(ihmclass =
		  load_class_bootstrap(utf_new_char("java/util/IdentityHashMap"))))
		return false;

	global_ref_table = GCNEW(jobject);

	if (!(*global_ref_table = native_new_and_init(ihmclass)))
		return false;

	if (!(getmid = class_resolvemethod(ihmclass, utf_get,
									   utf_java_lang_Object__java_lang_Object)))
		return false;

	if (!(putmid = class_resolvemethod(ihmclass, utf_put,
									   utf_new_char("(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"))))
		return false;

	if (!(removemid =
		  class_resolvemethod(ihmclass, utf_remove,
							  utf_java_lang_Object__java_lang_Object)))
		return false;


	/* direct buffer stuff */

	if (!(class_java_nio_Buffer =
		  load_class_bootstrap(utf_new_char("java/nio/Buffer"))) ||
		!link_class(class_java_nio_Buffer))
		return false;

	if (!(class_java_nio_DirectByteBufferImpl =
		  load_class_bootstrap(utf_new_char("java/nio/DirectByteBufferImpl"))) ||
		!link_class(class_java_nio_DirectByteBufferImpl))
		return false;

	if (!(class_java_nio_DirectByteBufferImpl_ReadWrite =
		  load_class_bootstrap(utf_new_char("java/nio/DirectByteBufferImpl$ReadWrite"))) ||
		!link_class(class_java_nio_DirectByteBufferImpl_ReadWrite))
		return false;

	if (!(dbbirw_init =
		class_resolvemethod(class_java_nio_DirectByteBufferImpl_ReadWrite,
							utf_init,
							utf_new_char("(Ljava/lang/Object;Lgnu/classpath/Pointer;III)V"))))
		return false;

#if SIZEOF_VOID_P == 8
	if (!(class_gnu_classpath_Pointer64 =
		  load_class_bootstrap(utf_new_char("gnu/classpath/Pointer64"))) ||
		!link_class(class_gnu_classpath_Pointer64))
		return false;
#else
	if (!(class_gnu_classpath_Pointer32 =
		  load_class_bootstrap(utf_new_char("gnu/classpath/Pointer32"))) ||
		!link_class(class_gnu_classpath_Pointer32))
		return false;
#endif

	return true;
}


static void fill_callblock_from_vargs(void *obj, methoddesc *descr,
									  jni_callblock blk[], va_list data,
									  s4 rettype)
{
	typedesc *paramtypes;
	s4        i;

	paramtypes = descr->paramtypes;

	/* if method is non-static fill first block and skip `this' pointer */

	i = 0;

	if (obj != NULL) {
		/* the `this' pointer */
		blk[0].itemtype = TYPE_ADR;
		blk[0].item = PTR_TO_ITEM(obj);

		paramtypes++;
		i++;
	} 

	for (; i < descr->paramcount; i++, paramtypes++) {
		switch (paramtypes->decltype) {
		/* primitive types */
		case PRIMITIVETYPE_BYTE:
		case PRIMITIVETYPE_CHAR:
		case PRIMITIVETYPE_SHORT: 
		case PRIMITIVETYPE_BOOLEAN: 
			blk[i].itemtype = TYPE_INT;
			blk[i].item = (s8) va_arg(data, s4);
			break;

		case PRIMITIVETYPE_INT:
			blk[i].itemtype = TYPE_INT;
			blk[i].item = (s8) va_arg(data, s4);
			break;

		case PRIMITIVETYPE_LONG:
			blk[i].itemtype = TYPE_LNG;
			blk[i].item = (s8) va_arg(data, s8);
			break;

		case PRIMITIVETYPE_FLOAT:
			blk[i].itemtype = TYPE_FLT;
#if defined(__ALPHA__)
			/* this keeps the assembler function much simpler */

			*((jdouble *) (&blk[i].item)) = (jdouble) va_arg(data, jdouble);
#else
			*((jfloat *) (&blk[i].item)) = (jfloat) va_arg(data, jdouble);
#endif
			break;

		case PRIMITIVETYPE_DOUBLE:
			blk[i].itemtype = TYPE_DBL;
			*((jdouble *) (&blk[i].item)) = (jdouble) va_arg(data, jdouble);
			break;

		case TYPE_ADR: 
			blk[i].itemtype = TYPE_ADR;
			blk[i].item = PTR_TO_ITEM(va_arg(data, void*));
			break;
		}
	}

	/* The standard doesn't say anything about return value checking,
	   but it appears to be useful. */

	if (rettype != descr->returntype.decltype)
		log_text("\n====\nWarning call*Method called for function with wrong return type\n====");
}


/* XXX it could be considered if we should do typechecking here in the future */

static bool fill_callblock_from_objectarray(void *obj, methoddesc *descr,
											jni_callblock blk[],
											java_objectarray *params)
{
    jobject    param;
	s4         paramcount;
	typedesc  *paramtypes;
	classinfo *c;
    s4         i;
	s4         j;

	paramcount = descr->paramcount;
	paramtypes = descr->paramtypes;

	/* if method is non-static fill first block and skip `this' pointer */

	i = 0;

	if (obj) {
		/* this pointer */
		blk[0].itemtype = TYPE_ADR;
		blk[0].item = PTR_TO_ITEM(obj);

		paramtypes++;
		paramcount--;
		i++;
	}

	for (j = 0; j < paramcount; i++, j++, paramtypes++) {
		switch (paramtypes->type) {
		/* primitive types */
		case TYPE_INT:
		case TYPE_LONG:
		case TYPE_FLOAT:
		case TYPE_DOUBLE:
			param = params->data[j];
			if (!param)
				goto illegal_arg;

			/* internally used data type */
			blk[i].itemtype = paramtypes->type;

			/* convert the value according to its declared type */

			c = param->vftbl->class;

			switch (paramtypes->decltype) {
			case PRIMITIVETYPE_BOOLEAN:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Boolean *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_BYTE:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Byte *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_CHAR:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Character *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_SHORT:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Short *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_BYTE].class_wrap)
					blk[i].item = (s8) ((java_lang_Byte *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_INT:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Integer *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_SHORT].class_wrap)
					blk[i].item = (s8) ((java_lang_Short *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_BYTE].class_wrap)
					blk[i].item = (s8) ((java_lang_Byte *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_LONG:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					blk[i].item = (s8) ((java_lang_Long *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_INT].class_wrap)
					blk[i].item = (s8) ((java_lang_Integer *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_SHORT].class_wrap)
					blk[i].item = (s8) ((java_lang_Short *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_BYTE].class_wrap)
					blk[i].item = (s8) ((java_lang_Byte *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_FLOAT:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					*((jfloat *) (&blk[i].item)) = (jfloat) ((java_lang_Float *) param)->value;
				else
					goto illegal_arg;
				break;

			case PRIMITIVETYPE_DOUBLE:
				if (c == primitivetype_table[paramtypes->decltype].class_wrap)
					*((jdouble *) (&blk[i].item)) = (jdouble) ((java_lang_Double *) param)->value;
				else if (c == primitivetype_table[PRIMITIVETYPE_FLOAT].class_wrap)
					*((jfloat *) (&blk[i].item)) = (jfloat) ((java_lang_Float *) param)->value;
				else
					goto illegal_arg;
				break;

			default:
				goto illegal_arg;
			} /* end declared type switch */
			break;
		
			case TYPE_ADDRESS:
				if (!resolve_class_from_typedesc(paramtypes, true, true, &c))
					return false;

				if (params->data[j] != 0) {
					if (paramtypes->arraydim > 0) {
						if (!builtin_arrayinstanceof(params->data[j], c))
							goto illegal_arg;

					} else {
						if (!builtin_instanceof(params->data[j], c))
							goto illegal_arg;
					}
				}
				blk[i].itemtype = TYPE_ADR;
				blk[i].item = PTR_TO_ITEM(params->data[j]);
				break;			

			default:
				goto illegal_arg;
		} /* end param type switch */

	} /* end param loop */

/*  	if (rettype) */
/*  		*rettype = descr->returntype.decltype; */

	return true;

illegal_arg:
	exceptions_throw_illegalargumentexception();
	return false;
}


/* _Jv_jni_CallObjectMethod ****************************************************

   Internal function to call Java Object methods.

*******************************************************************************/

static java_objectheader *_Jv_jni_CallObjectMethod(java_objectheader *o,
												   vftbl_t *vftbl,
												   methodinfo *m, va_list ap)
{
	methodinfo        *resm;
	s4                 paramcount;
	jni_callblock     *blk;
	java_objectheader *ret;
	s4                 dumpsize;

	STATISTICS(jniinvokation());

	if (m == NULL) {
		exceptions_throw_nullpointerexception();
		return NULL;
	}

	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap, TYPE_ADR);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2_ADR(ret, resm, paramcount,
							  paramcount * sizeof(jni_callblock),
							  blk);

	/* release dump area */

	dump_release(dumpsize);

	return ret;
}


/* _Jv_jni_CallIntMethod *******************************************************

   Internal function to call Java integer class methods (boolean,
   byte, char, short, int).

*******************************************************************************/

static jint _Jv_jni_CallIntMethod(java_objectheader *o, vftbl_t *vftbl,
								  methodinfo *m, va_list ap, s4 type)
{
	methodinfo    *resm;
	s4             paramcount;
	jni_callblock *blk;
	jint           ret;
	s4             dumpsize;

	STATISTICS(jniinvokation());

	if (m == NULL) {
		exceptions_throw_nullpointerexception();
		return 0;
	}
        
	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap, type);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2_INT(ret, resm, paramcount,
							  paramcount * sizeof(jni_callblock),
							  blk);

	/* release dump area */

	dump_release(dumpsize);

	return ret;
}


/* _Jv_jni_CallLongMethod ******************************************************

   Internal function to call Java long methods.

*******************************************************************************/

static jlong _Jv_jni_CallLongMethod(java_objectheader *o, vftbl_t *vftbl,
									methodinfo *m, va_list ap)
{
	methodinfo    *resm;
	s4             paramcount;
	jni_callblock *blk;
	jlong          ret;
	s4             dumpsize;

	STATISTICS(jniinvokation());

	if (m == NULL) {
		exceptions_throw_nullpointerexception();
		return 0;
	}

	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap, PRIMITIVETYPE_LONG);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2_LONG(ret, resm, paramcount,
							   paramcount * sizeof(jni_callblock),
							   blk);

	/* release dump area */

	dump_release(dumpsize);

	return ret;
}


/* _Jv_jni_CallFloatMethod *****************************************************

   Internal function to call Java float methods.

*******************************************************************************/

static jfloat _Jv_jni_CallFloatMethod(java_objectheader *o, vftbl_t *vftbl,
									  methodinfo *m, va_list ap)
{
	methodinfo    *resm;
	s4             paramcount;
	jni_callblock *blk;
	jdouble        ret;
	s4             dumpsize;

	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap,
							  PRIMITIVETYPE_FLOAT);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2_FLOAT(ret, resm, paramcount,
								paramcount * sizeof(jni_callblock),
								blk);

	/* release dump area */

	dump_release(dumpsize);

	return ret;
}


/* _Jv_jni_CallDoubleMethod ****************************************************

   Internal function to call Java double methods.

*******************************************************************************/

static jdouble _Jv_jni_CallDoubleMethod(java_objectheader *o, vftbl_t *vftbl,
										methodinfo *m, va_list ap)
{
	methodinfo    *resm;
	s4             paramcount;
	jni_callblock *blk;
	jfloat         ret;
	s4             dumpsize;

	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap,
							  PRIMITIVETYPE_DOUBLE);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2_DOUBLE(ret, resm, paramcount,
								 paramcount * sizeof(jni_callblock),
								 blk);

	/* release dump area */

	dump_release(dumpsize);

	return ret;
}


/* _Jv_jni_CallVoidMethod ******************************************************

   Internal function to call Java void methods.

*******************************************************************************/

static void _Jv_jni_CallVoidMethod(java_objectheader *o, vftbl_t *vftbl,
								   methodinfo *m, va_list ap)
{ 	
	methodinfo    *resm;
	s4             paramcount;
	jni_callblock *blk;
	s4             dumpsize;

	if (m == NULL) {
		exceptions_throw_nullpointerexception();
		return;
	}

	/* Class initialization is done by the JIT compiler.  This is ok
	   since a static method always belongs to the declaring class. */

	if (m->flags & ACC_STATIC) {
		/* For static methods we reset the object. */

		if (o != NULL)
			o = NULL;

		/* for convenience */

		resm = m;

	} else {
		/* For instance methods we make a virtual function table lookup. */

		resm = method_vftbl_lookup(vftbl, m);
	}

	/* mark start of dump memory area */

	dumpsize = dump_size();

	paramcount = resm->parseddesc->paramcount;

	blk = DMNEW(jni_callblock, paramcount);

	fill_callblock_from_vargs(o, resm->parseddesc, blk, ap, TYPE_VOID);

	STATISTICS(jnicallXmethodnvokation());

	ASM_CALLJAVAFUNCTION2(resm, paramcount,
						  paramcount * sizeof(jni_callblock),
						  blk);

	/* release dump area */

	dump_release(dumpsize);
}


/* _Jv_jni_invokeNative ********************************************************

   XXX

*******************************************************************************/

jobject *_Jv_jni_invokeNative(methodinfo *m, jobject obj,
							  java_objectarray *params)
{
	methodinfo        *resm;
	jni_callblock     *blk;
	java_objectheader *o;
	s4                 argcount;
	s4                 paramcount;

	if (m == NULL) {
		exceptions_throw_nullpointerexception();
		return NULL;
	}

	argcount = m->parseddesc->paramcount;
	paramcount = argcount;

	/* if method is non-static, remove the `this' pointer */

	if (!(m->flags & ACC_STATIC))
		paramcount--;

	/* the method is an instance method the obj has to be an instance of the 
	   class the method belongs to. For static methods the obj parameter
	   is ignored. */

	if (!(m->flags & ACC_STATIC) && obj &&
		(!builtin_instanceof((java_objectheader *) obj, m->class))) {
		*exceptionptr =
			new_exception_message(string_java_lang_IllegalArgumentException,
								  "Object parameter of wrong type in Java_java_lang_reflect_Method_invokeNative");
		return NULL;
	}

	if (((params == NULL) && (paramcount != 0)) ||
		(params && (params->header.size != paramcount))) {
		*exceptionptr =
			new_exception(string_java_lang_IllegalArgumentException);
		return NULL;
	}

	if (!(m->flags & ACC_STATIC) && (obj == NULL))  {
		*exceptionptr =
			new_exception_message(string_java_lang_NullPointerException,
								  "Static mismatch in Java_java_lang_reflect_Method_invokeNative");
		return NULL;
	}

	if ((m->flags & ACC_STATIC) && (obj != NULL))
		obj = NULL;

	/* For virtual calls with abstract method of interface classes we
	   have to do a virtual function table lookup (XXX TWISTI: not
	   sure if this is correct, took it from the old
	   implementation). */

	if ((obj != NULL) &&
		((m->flags & ACC_ABSTRACT) || (m->class->flags & ACC_INTERFACE))) {
		resm = method_vftbl_lookup(obj->vftbl, m);

	} else {
		/* just for convenience */

		resm = m;
	}

	blk = MNEW(jni_callblock, argcount);

	if (!fill_callblock_from_objectarray(obj, resm->parseddesc, blk,
										 params))
		return NULL;

	switch (resm->parseddesc->returntype.decltype) {
	case TYPE_VOID:
		ASM_CALLJAVAFUNCTION2(resm, argcount,
							  argcount * sizeof(jni_callblock),
							  blk);

		o = NULL;
		break;

	case PRIMITIVETYPE_BOOLEAN: {
		s4 i;
		java_lang_Boolean *bo;

		ASM_CALLJAVAFUNCTION2_INT(i, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);

		o = builtin_new(class_java_lang_Boolean);

		/* setting the value of the object direct */

		bo = (java_lang_Boolean *) o;
		bo->value = i;
	}
	break;

	case PRIMITIVETYPE_BYTE: {
		s4 i;
		java_lang_Byte *bo;

		ASM_CALLJAVAFUNCTION2_INT(i, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);

		o = builtin_new(class_java_lang_Byte);

		/* setting the value of the object direct */

		bo = (java_lang_Byte *) o;
		bo->value = i;
	}
	break;

	case PRIMITIVETYPE_CHAR: {
		s4 i;
		java_lang_Character *co;

		ASM_CALLJAVAFUNCTION2_INT(i, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);

		o = builtin_new(class_java_lang_Character);

		/* setting the value of the object direct */

		co = (java_lang_Character *) o;
		co->value = i;
	}
	break;

	case PRIMITIVETYPE_SHORT: {
		s4 i;
		java_lang_Short *so;

		ASM_CALLJAVAFUNCTION2_INT(i, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);

		o = builtin_new(class_java_lang_Short);

		/* setting the value of the object direct */

		so = (java_lang_Short *) o;
		so->value = i;
	}
	break;

	case PRIMITIVETYPE_INT: {
		s4 i;
		java_lang_Integer *io;

		ASM_CALLJAVAFUNCTION2_INT(i, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);

		o = builtin_new(class_java_lang_Integer);

		/* setting the value of the object direct */

		io = (java_lang_Integer *) o;
		io->value = i;
	}
	break;

	case PRIMITIVETYPE_LONG: {
		s8 l;
		java_lang_Long *lo;

		ASM_CALLJAVAFUNCTION2_LONG(l, resm, argcount,
								   argcount * sizeof(jni_callblock),
								   blk);

		o = builtin_new(class_java_lang_Long);

		/* setting the value of the object direct */

		lo = (java_lang_Long *) o;
		lo->value = l;
	}
	break;

	case PRIMITIVETYPE_FLOAT: {
		float f;
		java_lang_Float *fo;

		ASM_CALLJAVAFUNCTION2_FLOAT(f, resm, argcount,
									argcount * sizeof(jni_callblock),
									blk);

		o = builtin_new(class_java_lang_Float);

		/* setting the value of the object direct */

		fo = (java_lang_Float *) o;
		fo->value = f;
	}
	break;

	case PRIMITIVETYPE_DOUBLE: {
		double d;
		java_lang_Double *_do;

		ASM_CALLJAVAFUNCTION2_DOUBLE(d, resm, argcount,
									 argcount * sizeof(jni_callblock),
									 blk);

		o = builtin_new(class_java_lang_Double);

		/* setting the value of the object direct */

		_do = (java_lang_Double *) o;
		_do->value = d;
	}
	break;

	case TYPE_ADR:
		ASM_CALLJAVAFUNCTION2_ADR(o, resm, argcount,
								  argcount * sizeof(jni_callblock),
								  blk);
		break;

	default:
		/* if this happens the exception has already been set by
		   fill_callblock_from_objectarray */

		MFREE(blk, jni_callblock, argcount);

		return (jobject *) 0;
	}

	MFREE(blk, jni_callblock, argcount);

	if (*exceptionptr) {
		java_objectheader *cause;

		cause = *exceptionptr;

		/* clear exception pointer, we are calling JIT code again */

		*exceptionptr = NULL;

		*exceptionptr =
			new_exception_throwable(string_java_lang_reflect_InvocationTargetException,
									(java_lang_Throwable *) cause);
	}

	return (jobject *) o;
}


/* GetVersion ******************************************************************

   Returns the major version number in the higher 16 bits and the
   minor version number in the lower 16 bits.

*******************************************************************************/

jint GetVersion(JNIEnv *env)
{
	STATISTICS(jniinvokation());

	/* we support JNI 1.4 */

	return JNI_VERSION_1_4;
}


/* Class Operations ***********************************************************/

/* DefineClass *****************************************************************

   Loads a class from a buffer of raw class data. The buffer
   containing the raw class data is not referenced by the VM after the
   DefineClass call returns, and it may be discarded if desired.

*******************************************************************************/

jclass DefineClass(JNIEnv *env, const char *name, jobject loader,
				   const jbyte *buf, jsize bufLen)
{
	java_lang_ClassLoader *cl;
	java_lang_String      *s;
	java_bytearray        *ba;
	jclass                 c;

	STATISTICS(jniinvokation());

	cl = (java_lang_ClassLoader *) loader;
	s = javastring_new_char(name);
	ba = (java_bytearray *) buf;

	c = (jclass) Java_java_lang_VMClassLoader_defineClass(env, NULL, cl, s, ba,
														  0, bufLen, NULL);

	return (jclass) NewLocalRef(env, (jobject) c);
}


/* FindClass *******************************************************************

   This function loads a locally-defined class. It searches the
   directories and zip files specified by the CLASSPATH environment
   variable for the class with the specified name.

*******************************************************************************/

jclass FindClass(JNIEnv *env, const char *name)
{
	utf               *u;
	classinfo         *c;
	java_objectheader *cl;

	STATISTICS(jniinvokation());

	u = utf_new_char_classname((char *) name);

	/* Check stacktrace for classloader, if one found use it,
	    otherwise use the system classloader. */

#if defined(__ALPHA__) || defined(__ARM__) || defined(__I386__) || defined(__MIPS__) || defined(__POWERPC__) || defined(__X86_64__)
	/* these JITs support stacktraces, and so does the interpreter */

	cl = stacktrace_getCurrentClassLoader();
#else
# if defined(ENABLE_INTRP)
	/* the interpreter supports stacktraces, even if the JIT does not */

	if (opt_intrp)
		cl = stacktrace_getCurrentClassLoader();
	else
# endif
		cl = NULL;
#endif

	if (!(c = load_class_from_classloader(u, cl)))
		return NULL;

	if (!link_class(c))
		return NULL;

  	return (jclass) NewLocalRef(env, (jobject) c);
}
  

/* FromReflectedMethod *********************************************************

   Converts java.lang.reflect.Method or java.lang.reflect.Constructor
   object to a method ID.
  
*******************************************************************************/
  
jmethodID FromReflectedMethod(JNIEnv *env, jobject method)
{
	methodinfo *mi;
	classinfo  *c;
	s4          slot;

	STATISTICS(jniinvokation());

	if (method == NULL)
		return NULL;
	
	if (builtin_instanceof(method, class_java_lang_reflect_Method)) {
		java_lang_reflect_Method *rm;

		rm = (java_lang_reflect_Method *) method;
		c = (classinfo *) (rm->declaringClass);
		slot = rm->slot;

	} else if (builtin_instanceof(method, class_java_lang_reflect_Constructor)) {
		java_lang_reflect_Constructor *rc;

		rc = (java_lang_reflect_Constructor *) method;
		c = (classinfo *) (rc->clazz);
		slot = rc->slot;

	} else
		return NULL;

	if ((slot < 0) || (slot >= c->methodscount)) {
		/* this usually means a severe internal cacao error or somebody
		   tempered around with the reflected method */
		log_text("error illegal slot for method in class(FromReflectedMethod)");
		assert(0);
	}

	mi = &(c->methods[slot]);

	return mi;
}


/* GetSuperclass ***************************************************************

   If clazz represents any class other than the class Object, then
   this function returns the object that represents the superclass of
   the class specified by clazz.

*******************************************************************************/
 
jclass GetSuperclass(JNIEnv *env, jclass sub)
{
	classinfo *c;

	STATISTICS(jniinvokation());

	c = ((classinfo *) sub)->super.cls;

	if (!c)
		return NULL;

	return (jclass) NewLocalRef(env, (jobject) c);
}
  
 
/* IsAssignableFrom ************************************************************

   Determines whether an object of sub can be safely cast to sup.

*******************************************************************************/

jboolean IsAssignableFrom(JNIEnv *env, jclass sub, jclass sup)
{
	STATISTICS(jniinvokation());

	return Java_java_lang_VMClass_isAssignableFrom(env,
												   NULL,
												   (java_lang_Class *) sup,
												   (java_lang_Class *) sub);
}


/* Throw ***********************************************************************

   Causes a java.lang.Throwable object to be thrown.

*******************************************************************************/

jint Throw(JNIEnv *env, jthrowable obj)
{
	STATISTICS(jniinvokation());

	*exceptionptr = (java_objectheader *) obj;

	return JNI_OK;
}


/* ThrowNew ********************************************************************

   Constructs an exception object from the specified class with the
   message specified by message and causes that exception to be
   thrown.

*******************************************************************************/

jint ThrowNew(JNIEnv* env, jclass clazz, const char *msg) 
{
	java_lang_Throwable *o;
	java_lang_String    *s;

	STATISTICS(jniinvokation());

	s = (java_lang_String *) javastring_new_char(msg);

  	/* instantiate exception object */

	o = (java_lang_Throwable *) native_new_and_init_string((classinfo *) clazz,
														   s);

	if (!o)
		return -1;

	*exceptionptr = (java_objectheader *) o;

	return 0;
}


/* ExceptionOccurred ***********************************************************

   Determines if an exception is being thrown. The exception stays
   being thrown until either the native code calls ExceptionClear(),
   or the Java code handles the exception.

*******************************************************************************/

jthrowable ExceptionOccurred(JNIEnv *env)
{
	java_objectheader *e;

	STATISTICS(jniinvokation());

	e = *exceptionptr;

	return NewLocalRef(env, (jthrowable) e);
}


/* ExceptionDescribe ***********************************************************

   Prints an exception and a backtrace of the stack to a system
   error-reporting channel, such as stderr. This is a convenience
   routine provided for debugging.

*******************************************************************************/

void ExceptionDescribe(JNIEnv *env)
{
	java_objectheader *e;
	methodinfo        *m;

	STATISTICS(jniinvokation());

	e = *exceptionptr;

	if (e) {
		/* clear exception, because we are calling jit code again */

		*exceptionptr = NULL;

		/* get printStackTrace method from exception class */

		m = class_resolveclassmethod(e->vftbl->class,
									 utf_printStackTrace,
									 utf_void__void,
									 NULL,
									 true);

		if (!m)
			/* XXX what should we do? */
			return;

		/* print the stacktrace */

		ASM_CALLJAVAFUNCTION(m, e, NULL, NULL, NULL);
	}
}


/* ExceptionClear **************************************************************

   Clears any exception that is currently being thrown. If no
   exception is currently being thrown, this routine has no effect.

*******************************************************************************/

void ExceptionClear(JNIEnv *env)
{
	STATISTICS(jniinvokation());

	*exceptionptr = NULL;
}


/* FatalError ******************************************************************

   Raises a fatal error and does not expect the VM to recover. This
   function does not return.

*******************************************************************************/

void FatalError(JNIEnv *env, const char *msg)
{
	STATISTICS(jniinvokation());

	throw_cacao_exception_exit(string_java_lang_InternalError, msg);
}


/* PushLocalFrame **************************************************************

   Creates a new local reference frame, in which at least a given
   number of local references can be created.

*******************************************************************************/

jint PushLocalFrame(JNIEnv* env, jint capacity)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: PushLocalFrame: IMPLEMENT ME!");

	assert(0);

	return 0;
}

/* PopLocalFrame ***************************************************************

   Pops off the current local reference frame, frees all the local
   references, and returns a local reference in the previous local
   reference frame for the given result object.

*******************************************************************************/

jobject PopLocalFrame(JNIEnv* env, jobject result)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: PopLocalFrame: IMPLEMENT ME!");

	assert(0);

	/* add local reference and return the value */

	return NewLocalRef(env, NULL);
}


/* DeleteLocalRef **************************************************************

   Deletes the local reference pointed to by localRef.

*******************************************************************************/

void DeleteLocalRef(JNIEnv *env, jobject localRef)
{
	java_objectheader *o;
	localref_table    *lrt;
	s4                 i;

	STATISTICS(jniinvokation());

	o = (java_objectheader *) localRef;

	/* get local reference table (thread specific) */

	lrt = LOCALREFTABLE;

	/* remove the reference */

	for (i = 0; i < lrt->capacity; i++) {
		if (lrt->refs[i] == o) {
			lrt->refs[i] = NULL;
			lrt->used--;

			return;
		}
	}

	/* this should not happen */

/*  	if (opt_checkjni) */
/*  	FatalError(env, "Bad global or local ref passed to JNI"); */
	log_text("JNI-DeleteLocalRef: Bad global or local ref passed to JNI");
}


/* IsSameObject ****************************************************************

   Tests whether two references refer to the same Java object.

*******************************************************************************/

jboolean IsSameObject(JNIEnv *env, jobject ref1, jobject ref2)
{
	STATISTICS(jniinvokation());

	if (ref1 == ref2)
		return JNI_TRUE;
	else
		return JNI_FALSE;
}


/* NewLocalRef *****************************************************************

   Creates a new local reference that refers to the same object as ref.

*******************************************************************************/

jobject NewLocalRef(JNIEnv *env, jobject ref)
{
	localref_table *lrt;
	s4              i;

	STATISTICS(jniinvokation());

	if (ref == NULL)
		return NULL;

	/* get local reference table (thread specific) */

	lrt = LOCALREFTABLE;

	/* check if we have space for the requested reference */

	if (lrt->used == lrt->capacity) {
/* 		throw_cacao_exception_exit(string_java_lang_InternalError, */
/* 								   "Too many local references"); */
		fprintf(stderr, "Too many local references");
		assert(0);
	}

	/* insert the reference */

	for (i = 0; i < lrt->capacity; i++) {
		if (lrt->refs[i] == NULL) {
			lrt->refs[i] = (java_objectheader *) ref;
			lrt->used++;

			return ref;
		}
	}

	/* should not happen, just to be sure */

	assert(0);

	/* keep compiler happy */

	return NULL;
}


/* EnsureLocalCapacity *********************************************************

   Ensures that at least a given number of local references can be
   created in the current thread

*******************************************************************************/

jint EnsureLocalCapacity(JNIEnv* env, jint capacity)
{
	localref_table *lrt;

	STATISTICS(jniinvokation());

	/* get local reference table (thread specific) */

	lrt = LOCALREFTABLE;

	/* check if capacity elements are available in the local references table */

	if ((lrt->used + capacity) > lrt->capacity) {
		*exceptionptr = new_exception(string_java_lang_OutOfMemoryError);
		return -1;
	}

	return 0;
}


/* AllocObject *****************************************************************

   Allocates a new Java object without invoking any of the
   constructors for the object. Returns a reference to the object.

*******************************************************************************/

jobject AllocObject(JNIEnv *env, jclass clazz)
{
	java_objectheader *o;

	STATISTICS(jniinvokation());

	if ((clazz->flags & ACC_INTERFACE) || (clazz->flags & ACC_ABSTRACT)) {
		*exceptionptr =
			new_exception_utfmessage(string_java_lang_InstantiationException,
									 clazz->name);
		return NULL;
	}
		
	o = builtin_new(clazz);

	return NewLocalRef(env, o);
}


/* NewObject *******************************************************************

   Programmers place all arguments that are to be passed to the
   constructor immediately following the methodID
   argument. NewObject() accepts these arguments and passes them to
   the Java method that the programmer wishes to invoke.

*******************************************************************************/

jobject NewObject(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	java_objectheader *o;
	va_list            ap;

	STATISTICS(jniinvokation());

	/* create object */

	o = builtin_new(clazz);
	
	if (!o)
		return NULL;

	/* call constructor */

	va_start(ap, methodID);
	_Jv_jni_CallVoidMethod(o, o->vftbl, methodID, ap);
	va_end(ap);

	return NewLocalRef(env, o);
}


/* NewObjectV ******************************************************************

   Programmers place all arguments that are to be passed to the
   constructor in an args argument of type va_list that immediately
   follows the methodID argument. NewObjectV() accepts these
   arguments, and, in turn, passes them to the Java method that the
   programmer wishes to invoke.

*******************************************************************************/

jobject NewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args)
{
	java_objectheader *o;

	STATISTICS(jniinvokation());

	/* create object */

	o = builtin_new(clazz);
	
	if (!o)
		return NULL;

	/* call constructor */

	_Jv_jni_CallVoidMethod(o, o->vftbl, methodID, args);

	return NewLocalRef(env, o);
}


/*********************************************************************************** 

	Constructs a new Java object
	arguments that are to be passed to the constructor are placed in 
	args array of jvalues 

***********************************************************************************/

jobject NewObjectA(JNIEnv* env, jclass clazz, jmethodID methodID, jvalue *args)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: NewObjectA: IMPLEMENT ME!");

	return NewLocalRef(env, NULL);
}


/* GetObjectClass **************************************************************

 Returns the class of an object.

*******************************************************************************/

jclass GetObjectClass(JNIEnv *env, jobject obj)
{
	classinfo *c;

	STATISTICS(jniinvokation());
	
	if (!obj || !obj->vftbl)
		return NULL;

 	c = obj->vftbl->class;

	return (jclass) NewLocalRef(env, (jobject) c);
}


/* IsInstanceOf ****************************************************************

   Tests whether an object is an instance of a class.

*******************************************************************************/

jboolean IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz)
{
	STATISTICS(jniinvokation());

	return Java_java_lang_VMClass_isInstance(env,
											 NULL,
											 (java_lang_Class *) clazz,
											 (java_lang_Object *) obj);
}


/***************** converts a java.lang.reflect.Field to a field ID ***************/
 
jfieldID FromReflectedField(JNIEnv* env, jobject field)
{
	java_lang_reflect_Field *f;
	classinfo *c;
	jfieldID fid;   /* the JNI-fieldid of the wrapping object */

	STATISTICS(jniinvokation());

	/*log_text("JNI-Call: FromReflectedField");*/

	f=(java_lang_reflect_Field *)field;
	if (f==0) return 0;
	c=(classinfo*)(f->declaringClass);
	if ( (f->slot<0) || (f->slot>=c->fieldscount)) {
		/*this usually means a severe internal cacao error or somebody
		tempered around with the reflected method*/
		log_text("error illegal slot for field in class(FromReflectedField)");
		assert(0);
	}
	fid=&(c->fields[f->slot]);
	return fid;
}


/* ToReflectedMethod ***********************************************************

   Converts a method ID derived from cls to an instance of the
   java.lang.reflect.Method class or to an instance of the
   java.lang.reflect.Constructor class.

*******************************************************************************/

jobject ToReflectedMethod(JNIEnv* env, jclass cls, jmethodID methodID, jboolean isStatic)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: ToReflectedMethod: IMPLEMENT ME!");

	return NULL;
}


/* ToReflectedField ************************************************************

   Converts a field ID derived from cls to an instance of the
   java.lang.reflect.Field class.

*******************************************************************************/

jobject ToReflectedField(JNIEnv* env, jclass cls, jfieldID fieldID,
						 jboolean isStatic)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: ToReflectedField: IMPLEMENT ME!");

	return NULL;
}


/* Calling Instance Methods ***************************************************/

/* GetMethodID *****************************************************************

   Returns the method ID for an instance (nonstatic) method of a class
   or interface. The method may be defined in one of the clazz's
   superclasses and inherited by clazz. The method is determined by
   its name and signature.

   GetMethodID() causes an uninitialized class to be initialized.

*******************************************************************************/

jmethodID GetMethodID(JNIEnv* env, jclass clazz, const char *name,
					  const char *sig)
{
	classinfo  *c;
	utf        *uname;
	utf        *udesc;
	methodinfo *m;

	STATISTICS(jniinvokation());

	c = (classinfo *) clazz;

	if (!c)
		return NULL;

	if (!(c->state & CLASS_INITIALIZED))
		if (!initialize_class(c))
			return NULL;

	/* try to get the method of the class or one of it's superclasses */

	uname = utf_new_char((char *) name);
	udesc = utf_new_char((char *) sig);

 	m = class_resolvemethod(clazz, uname, udesc);

	if ((m == NULL) || (m->flags & ACC_STATIC)) {
		exceptions_throw_nosuchmethoderror(c, uname, udesc);

		return NULL;
	}

	return m;
}


/* JNI-functions for calling instance methods *********************************/

jobject CallObjectMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	java_objectheader* ret;
	va_list            ap;

	va_start(ap, methodID);
	ret = _Jv_jni_CallObjectMethod(obj, obj->vftbl, methodID, ap);
	va_end(ap);

	return NewLocalRef(env, ret);
}


jobject CallObjectMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	java_objectheader* ret;

	ret = _Jv_jni_CallObjectMethod(obj, obj->vftbl, methodID, args);

	return NewLocalRef(env, ret);
}


jobject CallObjectMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue * args)
{
	log_text("JNI-Call: CallObjectMethodA: IMPLEMENT ME!");

	return NewLocalRef(env, NULL);
}


jboolean CallBooleanMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list  ap;
	jboolean ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, ap,
								PRIMITIVETYPE_BOOLEAN);
	va_end(ap);

	return ret;
}


jboolean CallBooleanMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jboolean ret;

	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, args,
								PRIMITIVETYPE_BOOLEAN);

	return ret;
}


jboolean CallBooleanMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue * args)
{
	log_text("JNI-Call: CallBooleanMethodA");

	return 0;
}


jbyte CallByteMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jbyte   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, ap,
								PRIMITIVETYPE_BYTE);
	va_end(ap);

	return ret;

}

jbyte CallByteMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jbyte ret;

	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, args,
								PRIMITIVETYPE_BYTE);

	return ret;
}


jbyte CallByteMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallByteMethodA: IMPLEMENT ME!");

	return 0;
}


jchar CallCharMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jchar   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, ap,
								PRIMITIVETYPE_CHAR);
	va_end(ap);

	return ret;
}


jchar CallCharMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jchar ret;

	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, args,
								PRIMITIVETYPE_CHAR);

	return ret;
}


jchar CallCharMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallCharMethodA: IMPLEMENT ME!");

	return 0;
}


jshort CallShortMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jshort  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, ap,
								PRIMITIVETYPE_SHORT);
	va_end(ap);

	return ret;
}


jshort CallShortMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jshort ret;

	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, args,
								PRIMITIVETYPE_SHORT);

	return ret;
}


jshort CallShortMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallShortMethodA: IMPLEMENT ME!");

	return 0;
}



jint CallIntMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jint    ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, ap,
								PRIMITIVETYPE_INT);
	va_end(ap);

	return ret;
}


jint CallIntMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jint ret;

	ret = _Jv_jni_CallIntMethod(obj, obj->vftbl, methodID, args,
								PRIMITIVETYPE_INT);

	return ret;
}


jint CallIntMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallIntMethodA: IMPLEMENT ME!");

	return 0;
}



jlong CallLongMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jlong   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallLongMethod(obj, obj->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jlong CallLongMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jlong ret;

	ret = _Jv_jni_CallLongMethod(obj, obj->vftbl, methodID, args);

	return ret;
}


jlong CallLongMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallLongMethodA: IMPLEMENT ME!");

	return 0;
}



jfloat CallFloatMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jfloat  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallFloatMethod(obj, obj->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jfloat CallFloatMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jfloat ret;

	ret = _Jv_jni_CallFloatMethod(obj, obj->vftbl, methodID, args);

	return ret;
}


jfloat CallFloatMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallFloatMethodA: IMPLEMENT ME!");

	return 0;
}



jdouble CallDoubleMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;
	jdouble ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallDoubleMethod(obj, obj->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jdouble CallDoubleMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	jdouble ret;

	ret = _Jv_jni_CallDoubleMethod(obj, obj->vftbl, methodID, args);

	return ret;
}


jdouble CallDoubleMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallDoubleMethodA: IMPLEMENT ME!");

	return 0;
}



void CallVoidMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...)
{
	va_list ap;

	va_start(ap, methodID);
	_Jv_jni_CallVoidMethod(obj, obj->vftbl, methodID, ap);
	va_end(ap);
}


void CallVoidMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args)
{
	_Jv_jni_CallVoidMethod(obj, obj->vftbl, methodID, args);
}


void CallVoidMethodA(JNIEnv *env, jobject obj, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallVoidMethodA: IMPLEMENT ME!");
}



jobject CallNonvirtualObjectMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	java_objectheader *ret;
	va_list            ap;

	va_start(ap, methodID);
	ret = _Jv_jni_CallObjectMethod(obj, clazz->vftbl, methodID, ap);
	va_end(ap);

	return NewLocalRef(env, ret);
}


jobject CallNonvirtualObjectMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	java_objectheader* ret;

	ret = _Jv_jni_CallObjectMethod(obj, clazz->vftbl, methodID, args);

	return NewLocalRef(env, ret);
}


jobject CallNonvirtualObjectMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualObjectMethodA: IMPLEMENT ME!");

	return NewLocalRef(env, NULL);
}



jboolean CallNonvirtualBooleanMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list  ap;
	jboolean ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, ap,
								PRIMITIVETYPE_BOOLEAN);
	va_end(ap);

	return ret;

}


jboolean CallNonvirtualBooleanMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jboolean ret;

	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, args,
								PRIMITIVETYPE_BOOLEAN);

	return ret;
}


jboolean CallNonvirtualBooleanMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualBooleanMethodA: IMPLEMENT ME!");

	return 0;
}


jbyte CallNonvirtualByteMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jbyte   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, ap,
								PRIMITIVETYPE_BYTE);
	va_end(ap);

	return ret;
}


jbyte CallNonvirtualByteMethodV (JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jbyte ret;

	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, args,
								PRIMITIVETYPE_BYTE);

	return ret;
}


jbyte CallNonvirtualByteMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualByteMethodA: IMPLEMENT ME!");

	return 0;
}



jchar CallNonvirtualCharMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jchar   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, ap,
								PRIMITIVETYPE_CHAR);
	va_end(ap);

	return ret;
}


jchar CallNonvirtualCharMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jchar ret;

	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, args,
								PRIMITIVETYPE_CHAR);

	return ret;
}


jchar CallNonvirtualCharMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualCharMethodA: IMPLEMENT ME!");

	return 0;
}



jshort CallNonvirtualShortMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jshort  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, ap,
								PRIMITIVETYPE_SHORT);
	va_end(ap);

	return ret;
}


jshort CallNonvirtualShortMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jshort ret;

	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, args,
								PRIMITIVETYPE_SHORT);

	return ret;
}


jshort CallNonvirtualShortMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualShortMethodA: IMPLEMENT ME!");

	return 0;
}



jint CallNonvirtualIntMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jint    ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, ap,
								PRIMITIVETYPE_INT);
	va_end(ap);

	return ret;
}


jint CallNonvirtualIntMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jint ret;

	ret = _Jv_jni_CallIntMethod(obj, clazz->vftbl, methodID, args,
								PRIMITIVETYPE_INT);

	return ret;
}


jint CallNonvirtualIntMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualIntMethodA: IMPLEMENT ME!");

	return 0;
}



jlong CallNonvirtualLongMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jlong   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallLongMethod(obj, clazz->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jlong CallNonvirtualLongMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jlong ret;

	ret = _Jv_jni_CallLongMethod(obj, clazz->vftbl, methodID, args);

	return 0;
}


jlong CallNonvirtualLongMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualLongMethodA: IMPLEMENT ME!");

	return 0;
}



jfloat CallNonvirtualFloatMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jfloat  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallFloatMethod(obj, clazz->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jfloat CallNonvirtualFloatMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jfloat ret;

	ret = _Jv_jni_CallFloatMethod(obj, clazz->vftbl, methodID, args);

	return ret;
}


jfloat CallNonvirtualFloatMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: CallNonvirtualFloatMethodA: IMPLEMENT ME!");

	return 0;
}



jdouble CallNonvirtualDoubleMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jdouble ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallDoubleMethod(obj, clazz->vftbl, methodID, ap);
	va_end(ap);

	return ret;
}


jdouble CallNonvirtualDoubleMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	jdouble ret;

	ret = _Jv_jni_CallDoubleMethod(obj, clazz->vftbl, methodID, args);

	return ret;
}


jdouble CallNonvirtualDoubleMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallNonvirtualDoubleMethodA: IMPLEMENT ME!");

	return 0;
}



void CallNonvirtualVoidMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;

	va_start(ap, methodID);
	_Jv_jni_CallVoidMethod(obj, clazz->vftbl, methodID, ap);
	va_end(ap);
}


void CallNonvirtualVoidMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args)
{
	_Jv_jni_CallVoidMethod(obj, clazz->vftbl, methodID, args);
}


void CallNonvirtualVoidMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, jvalue * args)
{
	log_text("JNI-Call: CallNonvirtualVoidMethodA: IMPLEMENT ME!");
}


/* Accessing Fields of Objects ************************************************/

/* GetFieldID ******************************************************************

   Returns the field ID for an instance (nonstatic) field of a
   class. The field is specified by its name and signature. The
   Get<type>Field and Set<type>Field families of accessor functions
   use field IDs to retrieve object fields.

*******************************************************************************/

jfieldID GetFieldID(JNIEnv *env, jclass clazz, const char *name,
					const char *sig) 
{
	fieldinfo *f;
	utf       *uname;
	utf       *udesc;

	STATISTICS(jniinvokation());

	uname = utf_new_char((char *) name);
	udesc = utf_new_char((char *) sig);

	f = class_findfield(clazz, uname, udesc); 
	
	if (!f)
		*exceptionptr =	new_exception(string_java_lang_NoSuchFieldError);  

	return f;
}


/* Get<type>Field Routines *****************************************************

   This family of accessor routines returns the value of an instance
   (nonstatic) field of an object. The field to access is specified by
   a field ID obtained by calling GetFieldID().

*******************************************************************************/

jobject GetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	java_objectheader *o;

	STATISTICS(jniinvokation());

	o = GET_FIELD(obj, java_objectheader*, fieldID);

	return NewLocalRef(env, o);
}


jboolean GetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s4 i;

	STATISTICS(jniinvokation());

	i = GET_FIELD(obj, s4, fieldID);

	return (jboolean) i;
}


jbyte GetByteField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s4 i;

	STATISTICS(jniinvokation());

	i = GET_FIELD(obj, s4, fieldID);

	return (jbyte) i;
}


jchar GetCharField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s4 i;

	STATISTICS(jniinvokation());

	i = GET_FIELD(obj, s4, fieldID);

	return (jchar) i;
}


jshort GetShortField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s4 i;

	STATISTICS(jniinvokation());

	i = GET_FIELD(obj, s4, fieldID);

	return (jshort) i;
}


jint GetIntField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s4 i;

	STATISTICS(jniinvokation());

	i = GET_FIELD(obj, s4, fieldID);

	return i;
}


jlong GetLongField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	s8 l;

	STATISTICS(jniinvokation());

	l = GET_FIELD(obj, s8, fieldID);

	return l;
}


jfloat GetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	float f;

	STATISTICS(jniinvokation());

	f = GET_FIELD(obj, float, fieldID);

	return f;
}


jdouble GetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID)
{
	double d;

	STATISTICS(jniinvokation());

	d = GET_FIELD(obj, double, fieldID);

	return d;
}


/* Set<type>Field Routines *****************************************************

   This family of accessor routines sets the value of an instance
   (nonstatic) field of an object. The field to access is specified by
   a field ID obtained by calling GetFieldID().

*******************************************************************************/

void SetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID, jobject value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, java_objectheader*, fieldID, value);
}


void SetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID, jboolean value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s4, fieldID, value);
}


void SetByteField(JNIEnv *env, jobject obj, jfieldID fieldID, jbyte value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s4, fieldID, value);
}


void SetCharField(JNIEnv *env, jobject obj, jfieldID fieldID, jchar value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s4, fieldID, value);
}


void SetShortField(JNIEnv *env, jobject obj, jfieldID fieldID, jshort value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s4, fieldID, value);
}


void SetIntField(JNIEnv *env, jobject obj, jfieldID fieldID, jint value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s4, fieldID, value);
}


void SetLongField(JNIEnv *env, jobject obj, jfieldID fieldID, jlong value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, s8, fieldID, value);
}


void SetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID, jfloat value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, float, fieldID, value);
}


void SetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID, jdouble value)
{
	STATISTICS(jniinvokation());

	SET_FIELD(obj, double, fieldID, value);
}


/* Calling Static Methods *****************************************************/

/* GetStaticMethodID ***********************************************************

   Returns the method ID for a static method of a class. The method is
   specified by its name and signature.

   GetStaticMethodID() causes an uninitialized class to be
   initialized.

*******************************************************************************/

jmethodID GetStaticMethodID(JNIEnv *env, jclass clazz, const char *name,
							const char *sig)
{
	classinfo  *c;
	utf        *uname;
	utf        *udesc;
	methodinfo *m;

	STATISTICS(jniinvokation());

	c = (classinfo *) clazz;

	if (!c)
		return NULL;

	if (!(c->state & CLASS_INITIALIZED))
		if (!initialize_class(c))
			return NULL;

	/* try to get the static method of the class */

	uname = utf_new_char((char *) name);
	udesc = utf_new_char((char *) sig);

 	m = class_resolvemethod(c, uname, udesc);

	if ((m == NULL) || !(m->flags & ACC_STATIC)) {
		exceptions_throw_nosuchmethoderror(c, uname, udesc);

		return NULL;
	}

	return m;
}


jobject CallStaticObjectMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	java_objectheader *ret;
	va_list            ap;

	va_start(ap, methodID);
	ret = _Jv_jni_CallObjectMethod(NULL, NULL, methodID, ap);
	va_end(ap);

	return NewLocalRef(env, ret);
}


jobject CallStaticObjectMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	java_objectheader *ret;

	ret = _Jv_jni_CallObjectMethod(NULL, NULL, methodID, args);

	return NewLocalRef(env, ret);
}


jobject CallStaticObjectMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticObjectMethodA: IMPLEMENT ME!");

	return NewLocalRef(env, NULL);
}


jboolean CallStaticBooleanMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list  ap;
	jboolean ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, ap,
								PRIMITIVETYPE_BOOLEAN);
	va_end(ap);

	return ret;
}


jboolean CallStaticBooleanMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jboolean ret;

	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, args,
								PRIMITIVETYPE_BOOLEAN);

	return ret;
}


jboolean CallStaticBooleanMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticBooleanMethodA: IMPLEMENT ME!");

	return 0;
}


jbyte CallStaticByteMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jbyte   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, ap, PRIMITIVETYPE_BYTE);
	va_end(ap);

	return ret;
}


jbyte CallStaticByteMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jbyte ret;

	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, args, PRIMITIVETYPE_BYTE);

	return ret;
}


jbyte CallStaticByteMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticByteMethodA: IMPLEMENT ME!");

	return 0;
}


jchar CallStaticCharMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jchar   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, ap, PRIMITIVETYPE_CHAR);
	va_end(ap);

	return ret;
}


jchar CallStaticCharMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jchar ret;

	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, args, PRIMITIVETYPE_CHAR);

	return ret;
}


jchar CallStaticCharMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticCharMethodA: IMPLEMENT ME!");

	return 0;
}


jshort CallStaticShortMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jshort  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, ap, PRIMITIVETYPE_SHORT);
	va_end(ap);

	return ret;
}


jshort CallStaticShortMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jshort ret;

	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, args,
								PRIMITIVETYPE_SHORT);

	return ret;
}


jshort CallStaticShortMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticShortMethodA: IMPLEMENT ME!");

	return 0;
}


jint CallStaticIntMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jint    ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, ap, PRIMITIVETYPE_INT);
	va_end(ap);

	return ret;
}


jint CallStaticIntMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jint ret;

	ret = _Jv_jni_CallIntMethod(NULL, NULL, methodID, args, PRIMITIVETYPE_INT);

	return ret;
}


jint CallStaticIntMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticIntMethodA: IMPLEMENT ME!");

	return 0;
}


jlong CallStaticLongMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jlong   ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallLongMethod(NULL, NULL, methodID, ap);
	va_end(ap);

	return ret;
}


jlong CallStaticLongMethodV(JNIEnv *env, jclass clazz, jmethodID methodID,
							va_list args)
{
	jlong ret;
	
	ret = _Jv_jni_CallLongMethod(NULL, NULL, methodID, args);

	return ret;
}


jlong CallStaticLongMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticLongMethodA: IMPLEMENT ME!");

	return 0;
}



jfloat CallStaticFloatMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jfloat  ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallFloatMethod(NULL, NULL, methodID, ap);
	va_end(ap);

	return ret;
}


jfloat CallStaticFloatMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jfloat ret;

	ret = _Jv_jni_CallFloatMethod(NULL, NULL, methodID, args);

	return ret;
}


jfloat CallStaticFloatMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticFloatMethodA: IMPLEMENT ME!");

	return 0;
}


jdouble CallStaticDoubleMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;
	jdouble ret;

	va_start(ap, methodID);
	ret = _Jv_jni_CallDoubleMethod(NULL, NULL, methodID, ap);
	va_end(ap);

	return ret;
}


jdouble CallStaticDoubleMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	jdouble ret;

	ret = _Jv_jni_CallDoubleMethod(NULL, NULL, methodID, args);

	return ret;
}


jdouble CallStaticDoubleMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue *args)
{
	log_text("JNI-Call: CallStaticDoubleMethodA: IMPLEMENT ME!");

	return 0;
}


void CallStaticVoidMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...)
{
	va_list ap;

	va_start(ap, methodID);
	_Jv_jni_CallVoidMethod(NULL, NULL, methodID, ap);
	va_end(ap);
}


void CallStaticVoidMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args)
{
	_Jv_jni_CallVoidMethod(NULL, NULL, methodID, args);
}


void CallStaticVoidMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, jvalue * args)
{
	log_text("JNI-Call: CallStaticVoidMethodA: IMPLEMENT ME!");
}


/* Accessing Static Fields ****************************************************/

/* GetStaticFieldID ************************************************************

   Returns the field ID for a static field of a class. The field is
   specified by its name and signature. The GetStatic<type>Field and
   SetStatic<type>Field families of accessor functions use field IDs
   to retrieve static fields.

*******************************************************************************/

jfieldID GetStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig)
{
	jfieldID f;

	STATISTICS(jniinvokation());

	f = class_findfield(clazz,
						utf_new_char((char *) name),
						utf_new_char((char *) sig));
	
	if (!f)
		*exceptionptr =	new_exception(string_java_lang_NoSuchFieldError);

	return f;
}


/* GetStatic<type>Field ********************************************************

   This family of accessor routines returns the value of a static
   field of an object.

*******************************************************************************/

jobject GetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return NULL;

	return NewLocalRef(env, fieldID->value.a);
}


jboolean GetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return false;

	return fieldID->value.i;       
}


jbyte GetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0;

	return fieldID->value.i;       
}


jchar GetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0;

	return fieldID->value.i;       
}


jshort GetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0;

	return fieldID->value.i;       
}


jint GetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0;

	return fieldID->value.i;       
}


jlong GetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0;

	return fieldID->value.l;
}


jfloat GetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0.0;

 	return fieldID->value.f;
}


jdouble GetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return 0.0;

	return fieldID->value.d;
}


/*  SetStatic<type>Field *******************************************************

	This family of accessor routines sets the value of a static field
	of an object.

*******************************************************************************/

void SetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID, jobject value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.a = value;
}


void SetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID, jboolean value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.i = value;
}


void SetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID, jbyte value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.i = value;
}


void SetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID, jchar value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.i = value;
}


void SetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID, jshort value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.i = value;
}


void SetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID, jint value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.i = value;
}


void SetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID, jlong value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.l = value;
}


void SetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID, jfloat value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.f = value;
}


void SetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID, jdouble value)
{
	STATISTICS(jniinvokation());

	if (!(clazz->state & CLASS_INITIALIZED))
		if (!initialize_class(clazz))
			return;

	fieldID->value.d = value;
}


/* String Operations **********************************************************/

/* NewString *******************************************************************

   Create new java.lang.String object from an array of Unicode
   characters.

*******************************************************************************/

jstring NewString(JNIEnv *env, const jchar *buf, jsize len)
{
	java_lang_String *s;
	java_chararray   *a;
	u4                i;

	STATISTICS(jniinvokation());
	
	s = (java_lang_String *) builtin_new(class_java_lang_String);
	a = builtin_newarray_char(len);

	/* javastring or characterarray could not be created */
	if (!a || !s)
		return NULL;

	/* copy text */
	for (i = 0; i < len; i++)
		a->data[i] = buf[i];

	s->value = a;
	s->offset = 0;
	s->count = len;

	return (jstring) NewLocalRef(env, (jobject) s);
}


static jchar emptyStringJ[]={0,0};

/* GetStringLength *************************************************************

   Returns the length (the count of Unicode characters) of a Java
   string.

*******************************************************************************/

jsize GetStringLength(JNIEnv *env, jstring str)
{
	return ((java_lang_String *) str)->count;
}


/********************  convertes javastring to u2-array ****************************/
	
u2 *javastring_tou2(jstring so) 
{
	java_lang_String *s;
	java_chararray   *a;
	u2               *stringbuffer;
	u4                i;

	STATISTICS(jniinvokation());
	
	s = (java_lang_String *) so;

	if (!s)
		return NULL;

	a = s->value;

	if (!a)
		return NULL;

	/* allocate memory */

	stringbuffer = MNEW(u2, s->count + 1);

	/* copy text */

	for (i = 0; i < s->count; i++)
		stringbuffer[i] = a->data[s->offset + i];
	
	/* terminate string */

	stringbuffer[i] = '\0';

	return stringbuffer;
}


/* GetStringChars **************************************************************

   Returns a pointer to the array of Unicode characters of the
   string. This pointer is valid until ReleaseStringchars() is called.

*******************************************************************************/

const jchar *GetStringChars(JNIEnv *env, jstring str, jboolean *isCopy)
{	
	jchar *jc;

	STATISTICS(jniinvokation());

	jc = javastring_tou2(str);

	if (jc)	{
		if (isCopy)
			*isCopy = JNI_TRUE;

		return jc;
	}

	if (isCopy)
		*isCopy = JNI_TRUE;

	return emptyStringJ;
}


/* ReleaseStringChars **********************************************************

   Informs the VM that the native code no longer needs access to
   chars. The chars argument is a pointer obtained from string using
   GetStringChars().

*******************************************************************************/

void ReleaseStringChars(JNIEnv *env, jstring str, const jchar *chars)
{
	STATISTICS(jniinvokation());

	if (chars == emptyStringJ)
		return;

	MFREE(((jchar *) chars), jchar, ((java_lang_String *) str)->count + 1);
}


/* NewStringUTF ****************************************************************

   Constructs a new java.lang.String object from an array of UTF-8 characters.

*******************************************************************************/

jstring NewStringUTF(JNIEnv *env, const char *bytes)
{
	java_lang_String *s;

	STATISTICS(jniinvokation());

	s = javastring_new(utf_new_char(bytes));

    return (jstring) NewLocalRef(env, (jobject) s);
}


/****************** returns the utf8 length in bytes of a string *******************/

jsize GetStringUTFLength (JNIEnv *env, jstring string)
{   
    java_lang_String *s = (java_lang_String*) string;

	STATISTICS(jniinvokation());

    return (jsize) u2_utflength(s->value->data, s->count); 
}


/* GetStringUTFChars ***********************************************************

   Returns a pointer to an array of UTF-8 characters of the
   string. This array is valid until it is released by
   ReleaseStringUTFChars().

*******************************************************************************/

const char *GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy)
{
	utf *u;

	STATISTICS(jniinvokation());

	if (!string)
		return "";

	if (isCopy)
		*isCopy = JNI_TRUE;
	
	u = javastring_toutf((java_lang_String *) string, false);

	if (u)
		return u->text;

	return "";
}


/* ReleaseStringUTFChars *******************************************************

   Informs the VM that the native code no longer needs access to
   utf. The utf argument is a pointer derived from string using
   GetStringUTFChars().

*******************************************************************************/

void ReleaseStringUTFChars(JNIEnv *env, jstring string, const char *utf)
{
	STATISTICS(jniinvokation());

    /* XXX we don't release utf chars right now, perhaps that should be done 
	   later. Since there is always one reference the garbage collector will
	   never get them */
}


/* Array Operations ***********************************************************/

/* GetArrayLength **************************************************************

   Returns the number of elements in the array.

*******************************************************************************/

jsize GetArrayLength(JNIEnv *env, jarray array)
{
	STATISTICS(jniinvokation());

	return array->size;
}


/* NewObjectArray **************************************************************

   Constructs a new array holding objects in class elementClass. All
   elements are initially set to initialElement.

*******************************************************************************/

jobjectArray NewObjectArray(JNIEnv *env, jsize length, jclass elementClass, jobject initialElement)
{
	java_objectarray *oa;
	s4                i;

	STATISTICS(jniinvokation());

	if (length < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

    oa = builtin_anewarray(length, elementClass);

	if (!oa)
		return NULL;

	/* set all elements to initialElement */

	for (i = 0; i < length; i++)
		oa->data[i] = initialElement;

	return (jobjectArray) NewLocalRef(env, (jobject) oa);
}


jobject GetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index)
{
    jobject o;

	STATISTICS(jniinvokation());

	if (index >= array->header.size) {
		exceptions_throw_arrayindexoutofboundsexception();
		return NULL;
	}

	o = array->data[index];
    
    return NewLocalRef(env, o);
}


void SetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject val)
{
	java_objectarray  *oa;
	java_objectheader *o;

	STATISTICS(jniinvokation());

	oa = (java_objectarray *) array;
	o  = (java_objectheader *) val;

    if (index >= array->header.size) {
		exceptions_throw_arrayindexoutofboundsexception();
		return;
	}

	/* check if the class of value is a subclass of the element class
	   of the array */

	if (!builtin_canstore(oa, o)) {
		*exceptionptr = new_exception(string_java_lang_ArrayStoreException);

		return;
	}

	array->data[index] = val;
}


jbooleanArray NewBooleanArray(JNIEnv *env, jsize len)
{
	java_booleanarray *ba;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	ba = builtin_newarray_boolean(len);

	return (jbooleanArray) NewLocalRef(env, (jobject) ba);
}


jbyteArray NewByteArray(JNIEnv *env, jsize len)
{
	java_bytearray *ba;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	ba = builtin_newarray_byte(len);

	return (jbyteArray) NewLocalRef(env, (jobject) ba);
}


jcharArray NewCharArray(JNIEnv *env, jsize len)
{
	java_chararray *ca;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	ca = builtin_newarray_char(len);

	return (jcharArray) NewLocalRef(env, (jobject) ca);
}


jshortArray NewShortArray(JNIEnv *env, jsize len)
{
	java_shortarray *sa;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	sa = builtin_newarray_short(len);

	return (jshortArray) NewLocalRef(env, (jobject) sa);
}


jintArray NewIntArray(JNIEnv *env, jsize len)
{
	java_intarray *ia;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	ia = builtin_newarray_int(len);

	return (jintArray) NewLocalRef(env, (jobject) ia);
}


jlongArray NewLongArray(JNIEnv *env, jsize len)
{
	java_longarray *la;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	la = builtin_newarray_long(len);

	return (jlongArray) NewLocalRef(env, (jobject) la);
}


jfloatArray NewFloatArray(JNIEnv *env, jsize len)
{
	java_floatarray *fa;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	fa = builtin_newarray_float(len);

	return (jfloatArray) NewLocalRef(env, (jobject) fa);
}


jdoubleArray NewDoubleArray(JNIEnv *env, jsize len)
{
	java_doublearray *da;

	STATISTICS(jniinvokation());

	if (len < 0) {
		exceptions_throw_negativearraysizeexception();
		return NULL;
	}

	da = builtin_newarray_double(len);

	return (jdoubleArray) NewLocalRef(env, (jobject) da);
}


/* Get<PrimitiveType>ArrayElements *********************************************

   A family of functions that returns the body of the primitive array.

*******************************************************************************/

jboolean *GetBooleanArrayElements(JNIEnv *env, jbooleanArray array,
								  jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jbyte *GetByteArrayElements(JNIEnv *env, jbyteArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jchar *GetCharArrayElements(JNIEnv *env, jcharArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jshort *GetShortArrayElements(JNIEnv *env, jshortArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jint *GetIntArrayElements(JNIEnv *env, jintArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jlong *GetLongArrayElements(JNIEnv *env, jlongArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jfloat *GetFloatArrayElements(JNIEnv *env, jfloatArray array, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


jdouble *GetDoubleArrayElements(JNIEnv *env, jdoubleArray array,
								jboolean *isCopy)
{
	STATISTICS(jniinvokation());

    if (isCopy)
		*isCopy = JNI_FALSE;

    return array->data;
}


/* Release<PrimitiveType>ArrayElements *****************************************

   A family of functions that informs the VM that the native code no
   longer needs access to elems. The elems argument is a pointer
   derived from array using the corresponding
   Get<PrimitiveType>ArrayElements() function. If necessary, this
   function copies back all changes made to elems to the original
   array.

*******************************************************************************/

void ReleaseBooleanArrayElements(JNIEnv *env, jbooleanArray array,
								 jboolean *elems, jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseByteArrayElements(JNIEnv *env, jbyteArray array, jbyte *elems,
							  jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseCharArrayElements(JNIEnv *env, jcharArray array, jchar *elems,
							  jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseShortArrayElements(JNIEnv *env, jshortArray array, jshort *elems,
							   jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseIntArrayElements(JNIEnv *env, jintArray array, jint *elems,
							 jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseLongArrayElements(JNIEnv *env, jlongArray array, jlong *elems,
							  jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseFloatArrayElements(JNIEnv *env, jfloatArray array, jfloat *elems,
							   jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


void ReleaseDoubleArrayElements(JNIEnv *env, jdoubleArray array,
								jdouble *elems, jint mode)
{
	STATISTICS(jniinvokation());

	if (elems != array->data) {
		switch (mode) {
		case JNI_COMMIT:
			MCOPY(array->data, elems, jboolean, array->header.size);
			break;
		case 0:
			MCOPY(array->data, elems, jboolean, array->header.size);
			/* XXX TWISTI how should it be freed? */
			break;
		case JNI_ABORT:
			/* XXX TWISTI how should it be freed? */
			break;
		}
	}
}


/*  Get<PrimitiveType>ArrayRegion **********************************************

	A family of functions that copies a region of a primitive array
	into a buffer.

*******************************************************************************/

void GetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start,
						   jsize len, jboolean *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jboolean, len);
}


void GetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len,
						jbyte *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size) 
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jbyte, len);
}


void GetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len,
						jchar *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jchar, len);
}


void GetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start,
						 jsize len, jshort *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else	
		MCOPY(buf, &array->data[start], jshort, len);
}


void GetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len,
					   jint *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jint, len);
}


void GetLongArrayRegion(JNIEnv *env, jlongArray array, jsize start, jsize len,
						jlong *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jlong, len);
}


void GetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start,
						 jsize len, jfloat *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jfloat, len);
}


void GetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start,
						  jsize len, jdouble *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start+len>array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(buf, &array->data[start], jdouble, len);
}


/*  Set<PrimitiveType>ArrayRegion **********************************************

	A family of functions that copies back a region of a primitive
	array from a buffer.

*******************************************************************************/

void SetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start,
						   jsize len, jboolean *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jboolean, len);
}


void SetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len,
						jbyte *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jbyte, len);
}


void SetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len,
						jchar *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jchar, len);
}


void SetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start,
						 jsize len, jshort *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jshort, len);
}


void SetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len,
					   jint *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jint, len);
}


void SetLongArrayRegion(JNIEnv* env, jlongArray array, jsize start, jsize len,
						jlong *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jlong, len);
}


void SetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start,
						 jsize len, jfloat *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jfloat, len);
}


void SetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start,
						  jsize len, jdouble *buf)
{
	STATISTICS(jniinvokation());

    if (start < 0 || len < 0 || start + len > array->header.size)
		exceptions_throw_arrayindexoutofboundsexception();
    else
		MCOPY(&array->data[start], buf, jdouble, len);
}


/* Registering Native Methods *************************************************/

/* RegisterNatives *************************************************************

   Registers native methods with the class specified by the clazz
   argument. The methods parameter specifies an array of
   JNINativeMethod structures that contain the names, signatures, and
   function pointers of the native methods. The nMethods parameter
   specifies the number of native methods in the array.

*******************************************************************************/

jint RegisterNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods,
					 jint nMethods)
{
	STATISTICS(jniinvokation());

    log_text("JNI-Call: RegisterNatives: IMPLEMENT ME!!!");

    return 0;
}


/* UnregisterNatives ***********************************************************

   Unregisters native methods of a class. The class goes back to the
   state before it was linked or registered with its native method
   functions.

   This function should not be used in normal native code. Instead, it
   provides special programs a way to reload and relink native
   libraries.

*******************************************************************************/

jint UnregisterNatives(JNIEnv *env, jclass clazz)
{
	STATISTICS(jniinvokation());

	/* XXX TWISTI hmm, maybe we should not support that (like kaffe) */

    log_text("JNI-Call: UnregisterNatives: IMPLEMENT ME!!!");

    return 0;
}


/* Monitor Operations *********************************************************/

/* MonitorEnter ****************************************************************

   Enters the monitor associated with the underlying Java object
   referred to by obj.

*******************************************************************************/

jint MonitorEnter(JNIEnv *env, jobject obj)
{
	STATISTICS(jniinvokation());

	if (!obj) {
		exceptions_throw_nullpointerexception();
		return JNI_ERR;
	}

#if defined(USE_THREADS)
	builtin_monitorenter(obj);
#endif

	return JNI_OK;
}


/* MonitorExit *****************************************************************

   The current thread must be the owner of the monitor associated with
   the underlying Java object referred to by obj. The thread
   decrements the counter indicating the number of times it has
   entered this monitor. If the value of the counter becomes zero, the
   current thread releases the monitor.

*******************************************************************************/

jint MonitorExit(JNIEnv *env, jobject obj)
{
	STATISTICS(jniinvokation());

	if (!obj) {
		exceptions_throw_nullpointerexception();
		return JNI_ERR;
	}

#if defined(USE_THREADS)
	builtin_monitorexit(obj);
#endif

	return JNI_OK;
}


/* JavaVM Interface ***********************************************************/

/* GetJavaVM *******************************************************************

   Returns the Java VM interface (used in the Invocation API)
   associated with the current thread. The result is placed at the
   location pointed to by the second argument, vm.

*******************************************************************************/

jint GetJavaVM(JNIEnv *env, JavaVM **vm)
{
	STATISTICS(jniinvokation());

    *vm = &ptr_jvm;

	return 0;
}


void GetStringRegion(JNIEnv* env, jstring str, jsize start, jsize len, jchar *buf)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: GetStringRegion: IMPLEMENT ME!");
}


void GetStringUTFRegion (JNIEnv* env, jstring str, jsize start, jsize len, char *buf)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: GetStringUTFRegion: IMPLEMENT ME!");
}


/* GetPrimitiveArrayCritical ***************************************************

   Obtain a direct pointer to array elements.

*******************************************************************************/

void *GetPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy)
{
	java_bytearray *ba;
	jbyte          *bp;

	ba = (java_bytearray *) array;

	/* do the same as Kaffe does */

	bp = GetByteArrayElements(env, ba, isCopy);

	return (void *) bp;
}


/* ReleasePrimitiveArrayCritical ***********************************************

   No specific documentation.

*******************************************************************************/

void ReleasePrimitiveArrayCritical(JNIEnv *env, jarray array, void *carray,
								   jint mode)
{
	STATISTICS(jniinvokation());

	/* do the same as Kaffe does */

	ReleaseByteArrayElements(env, (jbyteArray) array, (jbyte *) carray, mode);
}


/* GetStringCritical ***********************************************************

   The semantics of these two functions are similar to the existing
   Get/ReleaseStringChars functions.

*******************************************************************************/

const jchar *GetStringCritical(JNIEnv *env, jstring string, jboolean *isCopy)
{
	STATISTICS(jniinvokation());

	return GetStringChars(env, string, isCopy);
}


void ReleaseStringCritical(JNIEnv *env, jstring string, const jchar *cstring)
{
	STATISTICS(jniinvokation());

	ReleaseStringChars(env, string, cstring);
}


jweak NewWeakGlobalRef(JNIEnv* env, jobject obj)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: NewWeakGlobalRef: IMPLEMENT ME!");

	return obj;
}


void DeleteWeakGlobalRef(JNIEnv* env, jweak ref)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: DeleteWeakGlobalRef: IMPLEMENT ME");
}


/* NewGlobalRef ****************************************************************

   Creates a new global reference to the object referred to by the obj
   argument.

*******************************************************************************/
    
jobject NewGlobalRef(JNIEnv* env, jobject lobj)
{
	java_objectheader *o;
	java_lang_Integer *refcount;
	java_objectheader *newval;

	STATISTICS(jniinvokation());

#if defined(USE_THREADS)
	builtin_monitorenter(*global_ref_table);
#endif
	
	ASM_CALLJAVAFUNCTION_ADR(o, getmid, *global_ref_table, lobj, NULL, NULL);

	refcount = (java_lang_Integer *) o;

	if (refcount == NULL) {
		newval = native_new_and_init_int(class_java_lang_Integer, 1);

		if (newval == NULL) {
#if defined(USE_THREADS)
			builtin_monitorexit(*global_ref_table);
#endif
			return NULL;
		}

		ASM_CALLJAVAFUNCTION(putmid, *global_ref_table, lobj, newval, NULL);

	} else {
		/* we can access the object itself, as we are in a
           synchronized section */

		refcount->value++;
	}

#if defined(USE_THREADS)
	builtin_monitorexit(*global_ref_table);
#endif

	return lobj;
}


/* DeleteGlobalRef *************************************************************

   Deletes the global reference pointed to by globalRef.

*******************************************************************************/

void DeleteGlobalRef(JNIEnv* env, jobject globalRef)
{
	java_objectheader *o;
	java_lang_Integer *refcount;
	s4                 val;

	STATISTICS(jniinvokation());

#if defined(USE_THREADS)
	builtin_monitorenter(*global_ref_table);
#endif

	ASM_CALLJAVAFUNCTION_ADR(o, getmid, *global_ref_table, globalRef, NULL,
							 NULL);

	refcount = (java_lang_Integer *) o;

	if (refcount == NULL) {
		log_text("JNI-DeleteGlobalRef: unable to find global reference");
		return;
	}

	/* we can access the object itself, as we are in a synchronized
	   section */

	val = refcount->value - 1;

	if (val == 0) {
		ASM_CALLJAVAFUNCTION(removemid, *global_ref_table, refcount, NULL,
							 NULL);

	} else {
		/* we do not create a new object, but set the new value into
           the old one */

		refcount->value = val;
	}

#if defined(USE_THREADS)
	builtin_monitorexit(*global_ref_table);
#endif
}


/* ExceptionCheck **************************************************************

   Returns JNI_TRUE when there is a pending exception; otherwise,
   returns JNI_FALSE.

*******************************************************************************/

jboolean ExceptionCheck(JNIEnv *env)
{
	STATISTICS(jniinvokation());

	return *exceptionptr ? JNI_TRUE : JNI_FALSE;
}


/* New JNI 1.4 functions ******************************************************/

/* NewDirectByteBuffer *********************************************************

   Allocates and returns a direct java.nio.ByteBuffer referring to the
   block of memory starting at the memory address address and
   extending capacity bytes.

*******************************************************************************/

jobject NewDirectByteBuffer(JNIEnv *env, void *address, jlong capacity)
{
	java_objectheader       *nbuf;
#if SIZEOF_VOID_P == 8
	gnu_classpath_Pointer64 *paddress;
#else
	gnu_classpath_Pointer32 *paddress;
#endif

	STATISTICS(jniinvokation());

	/* alocate a gnu.classpath.Pointer{32,64} object */

#if SIZEOF_VOID_P == 8
	if (!(paddress = (gnu_classpath_Pointer64 *)
		  builtin_new(class_gnu_classpath_Pointer64)))
#else
	if (!(paddress = (gnu_classpath_Pointer32 *)
		  builtin_new(class_gnu_classpath_Pointer32)))
#endif
		return NULL;

	/* fill gnu.classpath.Pointer{32,64} with address */

	paddress->data = (ptrint) address;

	/* create a java.nio.DirectByteBufferImpl$ReadWrite object */

	nbuf = (*env)->NewObject(env, class_java_nio_DirectByteBufferImpl_ReadWrite,
							 dbbirw_init, NULL, paddress,
							 (jint) capacity, (jint) capacity, (jint) 0);

	/* add local reference and return the value */

	return NewLocalRef(env, nbuf);
}


/* GetDirectBufferAddress ******************************************************

   Fetches and returns the starting address of the memory region
   referenced by the given direct java.nio.Buffer.

*******************************************************************************/

void *GetDirectBufferAddress(JNIEnv *env, jobject buf)
{
	java_nio_DirectByteBufferImpl *nbuf;
#if SIZEOF_VOID_P == 8
	gnu_classpath_Pointer64       *address;
#else
	gnu_classpath_Pointer32       *address;
#endif

	STATISTICS(jniinvokation());

	if (!builtin_instanceof(buf, class_java_nio_Buffer))
		return NULL;

	nbuf = (java_nio_DirectByteBufferImpl *) buf;

#if SIZEOF_VOID_P == 8
	address = (gnu_classpath_Pointer64 *) nbuf->address;
#else
	address = (gnu_classpath_Pointer32 *) nbuf->address;
#endif

	return (void *) address->data;
}


/* GetDirectBufferCapacity *****************************************************

   Fetches and returns the capacity in bytes of the memory region
   referenced by the given direct java.nio.Buffer.

*******************************************************************************/

jlong GetDirectBufferCapacity(JNIEnv* env, jobject buf)
{
	java_nio_Buffer *nbuf;

	STATISTICS(jniinvokation());

	if (!builtin_instanceof(buf, class_java_nio_DirectByteBufferImpl))
		return -1;

	nbuf = (java_nio_Buffer *) buf;

	return (jlong) nbuf->cap;
}


jint DestroyJavaVM(JavaVM *vm)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: DestroyJavaVM: IMPLEMENT ME!");

	return 0;
}


/* AttachCurrentThread *********************************************************

   Attaches the current thread to a Java VM. Returns a JNI interface
   pointer in the JNIEnv argument.

   Trying to attach a thread that is already attached is a no-op.

   A native thread cannot be attached simultaneously to two Java VMs.

   When a thread is attached to the VM, the context class loader is
   the bootstrap loader.

*******************************************************************************/

jint AttachCurrentThread(JavaVM *vm, void **env, void *thr_args)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: AttachCurrentThread: IMPLEMENT ME!");

#if !defined(HAVE___THREAD)
/*	cacao_thread_attach();*/
#else
	#error "No idea how to implement that. Perhaps Stefan knows"
#endif

	*env = &ptr_env;

	return 0;
}


jint DetachCurrentThread(JavaVM *vm)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: DetachCurrentThread: IMPLEMENT ME!");

	return 0;
}


/* GetEnv **********************************************************************

   If the current thread is not attached to the VM, sets *env to NULL,
   and returns JNI_EDETACHED. If the specified version is not
   supported, sets *env to NULL, and returns JNI_EVERSION. Otherwise,
   sets *env to the appropriate interface, and returns JNI_OK.

*******************************************************************************/

jint GetEnv(JavaVM *vm, void **env, jint version)
{
	STATISTICS(jniinvokation());

#if defined(USE_THREADS) && defined(NATIVE_THREADS)
	if (thread_getself() == NULL) {
		*env = NULL;

		return JNI_EDETACHED;
	}
#endif

	if ((version == JNI_VERSION_1_1) || (version == JNI_VERSION_1_2) ||
		(version == JNI_VERSION_1_4)) {
		*env = &ptr_env;

		return JNI_OK;
	}

#if defined(ENABLE_JVMTI)
	if (version == JVMTI_VERSION_1_0) {
		*env = (void *) new_jvmtienv();

		if (env != NULL)
			return JNI_OK;
	}
#endif
	
	*env = NULL;

	return JNI_EVERSION;
}



jint AttachCurrentThreadAsDaemon(JavaVM *vm, void **par1, void *par2)
{
	STATISTICS(jniinvokation());

	log_text("JNI-Call: AttachCurrentThreadAsDaemon: IMPLEMENT ME!");

	return 0;
}


/* JNI invocation table *******************************************************/

const struct JNIInvokeInterface JNI_JavaVMTable = {
	NULL,
	NULL,
	NULL,

	DestroyJavaVM,
	AttachCurrentThread,
	DetachCurrentThread,
	GetEnv,
	AttachCurrentThreadAsDaemon
};


/* JNI function table *********************************************************/

struct JNINativeInterface JNI_JNIEnvTable = {
	NULL,
	NULL,
	NULL,
	NULL,    
	&GetVersion,

	&DefineClass,
	&FindClass,
	&FromReflectedMethod,
	&FromReflectedField,
	&ToReflectedMethod,
	&GetSuperclass,
	&IsAssignableFrom,
	&ToReflectedField,

	&Throw,
	&ThrowNew,
	&ExceptionOccurred,
	&ExceptionDescribe,
	&ExceptionClear,
	&FatalError,
	&PushLocalFrame,
	&PopLocalFrame,

	&NewGlobalRef,
	&DeleteGlobalRef,
	&DeleteLocalRef,
	&IsSameObject,
	&NewLocalRef,
	&EnsureLocalCapacity,

	&AllocObject,
	&NewObject,
	&NewObjectV,
	&NewObjectA,

	&GetObjectClass,
	&IsInstanceOf,

	&GetMethodID,

	&CallObjectMethod,
	&CallObjectMethodV,
	&CallObjectMethodA,
	&CallBooleanMethod,
	&CallBooleanMethodV,
	&CallBooleanMethodA,
	&CallByteMethod,
	&CallByteMethodV,
	&CallByteMethodA,
	&CallCharMethod,
	&CallCharMethodV,
	&CallCharMethodA,
	&CallShortMethod,
	&CallShortMethodV,
	&CallShortMethodA,
	&CallIntMethod,
	&CallIntMethodV,
	&CallIntMethodA,
	&CallLongMethod,
	&CallLongMethodV,
	&CallLongMethodA,
	&CallFloatMethod,
	&CallFloatMethodV,
	&CallFloatMethodA,
	&CallDoubleMethod,
	&CallDoubleMethodV,
	&CallDoubleMethodA,
	&CallVoidMethod,
	&CallVoidMethodV,
	&CallVoidMethodA,

	&CallNonvirtualObjectMethod,
	&CallNonvirtualObjectMethodV,
	&CallNonvirtualObjectMethodA,
	&CallNonvirtualBooleanMethod,
	&CallNonvirtualBooleanMethodV,
	&CallNonvirtualBooleanMethodA,
	&CallNonvirtualByteMethod,
	&CallNonvirtualByteMethodV,
	&CallNonvirtualByteMethodA,
	&CallNonvirtualCharMethod,
	&CallNonvirtualCharMethodV,
	&CallNonvirtualCharMethodA,
	&CallNonvirtualShortMethod,
	&CallNonvirtualShortMethodV,
	&CallNonvirtualShortMethodA,
	&CallNonvirtualIntMethod,
	&CallNonvirtualIntMethodV,
	&CallNonvirtualIntMethodA,
	&CallNonvirtualLongMethod,
	&CallNonvirtualLongMethodV,
	&CallNonvirtualLongMethodA,
	&CallNonvirtualFloatMethod,
	&CallNonvirtualFloatMethodV,
	&CallNonvirtualFloatMethodA,
	&CallNonvirtualDoubleMethod,
	&CallNonvirtualDoubleMethodV,
	&CallNonvirtualDoubleMethodA,
	&CallNonvirtualVoidMethod,
	&CallNonvirtualVoidMethodV,
	&CallNonvirtualVoidMethodA,

	&GetFieldID,

	&GetObjectField,
	&GetBooleanField,
	&GetByteField,
	&GetCharField,
	&GetShortField,
	&GetIntField,
	&GetLongField,
	&GetFloatField,
	&GetDoubleField,
	&SetObjectField,
	&SetBooleanField,
	&SetByteField,
	&SetCharField,
	&SetShortField,
	&SetIntField,
	&SetLongField,
	&SetFloatField,
	&SetDoubleField,

	&GetStaticMethodID,

	&CallStaticObjectMethod,
	&CallStaticObjectMethodV,
	&CallStaticObjectMethodA,
	&CallStaticBooleanMethod,
	&CallStaticBooleanMethodV,
	&CallStaticBooleanMethodA,
	&CallStaticByteMethod,
	&CallStaticByteMethodV,
	&CallStaticByteMethodA,
	&CallStaticCharMethod,
	&CallStaticCharMethodV,
	&CallStaticCharMethodA,
	&CallStaticShortMethod,
	&CallStaticShortMethodV,
	&CallStaticShortMethodA,
	&CallStaticIntMethod,
	&CallStaticIntMethodV,
	&CallStaticIntMethodA,
	&CallStaticLongMethod,
	&CallStaticLongMethodV,
	&CallStaticLongMethodA,
	&CallStaticFloatMethod,
	&CallStaticFloatMethodV,
	&CallStaticFloatMethodA,
	&CallStaticDoubleMethod,
	&CallStaticDoubleMethodV,
	&CallStaticDoubleMethodA,
	&CallStaticVoidMethod,
	&CallStaticVoidMethodV,
	&CallStaticVoidMethodA,

	&GetStaticFieldID,

	&GetStaticObjectField,
	&GetStaticBooleanField,
	&GetStaticByteField,
	&GetStaticCharField,
	&GetStaticShortField,
	&GetStaticIntField,
	&GetStaticLongField,
	&GetStaticFloatField,
	&GetStaticDoubleField,
	&SetStaticObjectField,
	&SetStaticBooleanField,
	&SetStaticByteField,
	&SetStaticCharField,
	&SetStaticShortField,
	&SetStaticIntField,
	&SetStaticLongField,
	&SetStaticFloatField,
	&SetStaticDoubleField,

	&NewString,
	&GetStringLength,
	&GetStringChars,
	&ReleaseStringChars,

	&NewStringUTF,
	&GetStringUTFLength,
	&GetStringUTFChars,
	&ReleaseStringUTFChars,

	&GetArrayLength,

	&NewObjectArray,
	&GetObjectArrayElement,
	&SetObjectArrayElement,

	&NewBooleanArray,
	&NewByteArray,
	&NewCharArray,
	&NewShortArray,
	&NewIntArray,
	&NewLongArray,
	&NewFloatArray,
	&NewDoubleArray,

	&GetBooleanArrayElements,
	&GetByteArrayElements,
	&GetCharArrayElements,
	&GetShortArrayElements,
	&GetIntArrayElements,
	&GetLongArrayElements,
	&GetFloatArrayElements,
	&GetDoubleArrayElements,

	&ReleaseBooleanArrayElements,
	&ReleaseByteArrayElements,
	&ReleaseCharArrayElements,
	&ReleaseShortArrayElements,
	&ReleaseIntArrayElements,
	&ReleaseLongArrayElements,
	&ReleaseFloatArrayElements,
	&ReleaseDoubleArrayElements,

	&GetBooleanArrayRegion,
	&GetByteArrayRegion,
	&GetCharArrayRegion,
	&GetShortArrayRegion,
	&GetIntArrayRegion,
	&GetLongArrayRegion,
	&GetFloatArrayRegion,
	&GetDoubleArrayRegion,
	&SetBooleanArrayRegion,
	&SetByteArrayRegion,
	&SetCharArrayRegion,
	&SetShortArrayRegion,
	&SetIntArrayRegion,
	&SetLongArrayRegion,
	&SetFloatArrayRegion,
	&SetDoubleArrayRegion,

	&RegisterNatives,
	&UnregisterNatives,

	&MonitorEnter,
	&MonitorExit,

	&GetJavaVM,

	/* new JNI 1.2 functions */

	&GetStringRegion,
	&GetStringUTFRegion,

	&GetPrimitiveArrayCritical,
	&ReleasePrimitiveArrayCritical,

	&GetStringCritical,
	&ReleaseStringCritical,

	&NewWeakGlobalRef,
	&DeleteWeakGlobalRef,

	&ExceptionCheck,

	/* new JNI 1.4 functions */

	&NewDirectByteBuffer,
	&GetDirectBufferAddress,
	&GetDirectBufferCapacity
};


/* Invocation API Functions ***************************************************/

/* JNI_GetDefaultJavaVMInitArgs ************************************************

   Returns a default configuration for the Java VM.

*******************************************************************************/

jint JNI_GetDefaultJavaVMInitArgs(void *vm_args)
{
	JDK1_1InitArgs *_vm_args = (JDK1_1InitArgs *) vm_args;

	/* GNU classpath currently supports JNI 1.2 */

	_vm_args->version = JNI_VERSION_1_2;

	return 0;
}


/* JNI_GetCreatedJavaVMs *******************************************************

   Returns all Java VMs that have been created. Pointers to VMs are written in
   the buffer vmBuf in the order they are created. At most bufLen number of
   entries will be written. The total number of created VMs is returned in
   *nVMs.

*******************************************************************************/

jint JNI_GetCreatedJavaVMs(JavaVM **vmBuf, jsize bufLen, jsize *nVMs)
{
	log_text("JNI_GetCreatedJavaVMs: IMPLEMENT ME!!!");

	return 0;
}


/* JNI_CreateJavaVM ************************************************************

   Loads and initializes a Java VM. The current thread becomes the main thread.
   Sets the env argument to the JNI interface pointer of the main thread.

*******************************************************************************/

jint JNI_CreateJavaVM(JavaVM **p_vm, JNIEnv **p_env, void *vm_args)
{
	const struct JNIInvokeInterface *vm;
	struct JNINativeInterface *env;

	vm = &JNI_JavaVMTable;
	env = &JNI_JNIEnvTable;

	*p_vm = (JavaVM *) vm;
	*p_env = (JNIEnv *) env;

	return 0;
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
