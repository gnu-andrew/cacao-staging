/* src/native/vm/java_lang_Thread.c - java/lang/Thread functions

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

*/


#include "config.h"
#include "vm/types.h"

#include "native/jni.h"
#include "native/llni.h"
#include "native/native.h"

#include "native/include/java_lang_String.h"
#include "native/include/java_lang_Object.h"            /* java_lang_Thread.h */
#include "native/include/java_lang_Throwable.h"         /* java_lang_Thread.h */
#include "native/include/java_lang_Thread.h"

#if defined(ENABLE_JAVASE)
# include "native/include/java_lang_ThreadGroup.h"

# if defined(WITH_CLASSPATH_GNU)
#  include "native/include/java_lang_VMThread.h"
# endif
#endif

#include "threads/lock-common.h"
#include "threads/threads-common.h"

#include "toolbox/logging.h"

#include "vm/builtin.h"
#include "vm/exceptions.h"
#include "vm/stringlocal.h"

#include "vmcore/options.h"


/*
 * Class:     java/lang/Thread
 * Method:    countStackFrames
 * Signature: ()I
 */
s4 _Jv_java_lang_Thread_countStackFrames(java_lang_Thread *this)
{
    log_text("java_lang_Thread_countStackFrames called");

    return 0;
}


/*
 * Class:     java/lang/Thread
 * Method:    sleep
 * Signature: (J)V
 */
void _Jv_java_lang_Thread_sleep(s8 millis)
{
#if defined(ENABLE_THREADS)
	threads_sleep(millis, 0);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    start
 * Signature: (J)V
 */
void _Jv_java_lang_Thread_start(java_lang_Thread *this, s8 stacksize)
{
#if defined(ENABLE_THREADS)
	threads_thread_start((java_handle_t *) this);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    interrupt
 * Signature: ()V
 */
void _Jv_java_lang_Thread_interrupt(java_lang_Thread *this)
{
#if defined(ENABLE_THREADS)
	threadobject *thread;

#if defined(WITH_CLASSPATH_GNU)
	thread = (threadobject *) LLNI_field_direct(this, vmThread)->vmdata;
#elif defined(WITH_CLASSPATH_CLDC1_1)
	thread = (threadobject *) this->vm_thread;
#endif

	threads_thread_interrupt(thread);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    isInterrupted
 * Signature: ()Z
 */
s4 _Jv_java_lang_Thread_isInterrupted(java_lang_Thread *this)
{
#if defined(ENABLE_THREADS)
	threadobject *t;

# if defined(WITH_CLASSPATH_GNU)
	t = (threadobject *) LLNI_field_direct(this, vmThread)->vmdata;
# elif defined(WITH_CLASSPATH_SUN)
	/* XXX this is just a quick hack */

	for (t = threads_list_first(); t != NULL; t = threads_list_next(t)) {
		if (t->object == this)
			break;
	}
# elif defined(WITH_CLASSPATH_CLDC1_1)
	t = (threadobject *) this->vm_thread;
# else
#  error unknown classpath configuration
# endif

	return threads_thread_has_been_interrupted(t);
#else
	return 0;
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    suspend
 * Signature: ()V
 */
void _Jv_java_lang_Thread_suspend(java_lang_Thread *this)
{
#if defined(ENABLE_THREADS)
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    resume
 * Signature: ()V
 */
void _Jv_java_lang_Thread_resume(java_lang_Thread *this)
{
#if defined(ENABLE_THREADS)
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    setPriority
 * Signature: (I)V
 */
void _Jv_java_lang_Thread_setPriority(java_lang_Thread *this, s4 priority)
{
#if defined(ENABLE_THREADS)
	threadobject *t;

# if defined(WITH_CLASSPATH_GNU)
	t = (threadobject *) LLNI_field_direct(this, vmThread)->vmdata;
# elif defined(WITH_CLASSPATH_SUN)
	/* XXX this is just a quick hack */

	for (t = threads_list_first(); t != NULL; t = threads_list_next(t)) {
		if (t->object == this)
			break;
	}

	/* The threadobject is null when a thread is created in Java. The
	   priority is set later during startup. */

	if (t == NULL)
		return;
# elif defined(WITH_CLASSPATH_CLDC1_1)
	t = (threadobject *) this->vm_thread;

	/* The threadobject is null when a thread is created in Java. The
	   priority is set later during startup. */

	if (t == NULL)
		return;
# else
#  error unknown classpath configuration
# endif

	threads_set_thread_priority(t->tid, priority);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    stop
 * Signature: (Ljava/lang/Object;)V
 */
void _Jv_java_lang_Thread_stop(java_lang_Thread *this, java_lang_Throwable *t)
{
#if defined(ENABLE_THREADS)
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    currentThread
 * Signature: ()Ljava/lang/Thread;
 */
java_lang_Thread *_Jv_java_lang_Thread_currentThread(void)
{
#if defined(ENABLE_THREADS)
	threadobject          *thread;
#endif
	java_lang_Thread      *t;
#if defined(ENABLE_JAVASE)
	java_lang_ThreadGroup *group;
#endif

#if defined(ENABLE_THREADS)
	thread = THREADOBJECT;

	t = (java_lang_Thread *) threads_thread_get_object(thread);

	if (t == NULL)
		log_text("t ptr is NULL\n");

# if defined(ENABLE_JAVASE)
	LLNI_field_get_ref(t, group, group);

	if (group == NULL) {
		/* ThreadGroup of currentThread is not initialized */

		group = (java_lang_ThreadGroup *)
			native_new_and_init(class_java_lang_ThreadGroup);

		if (group == NULL)
			log_text("unable to create ThreadGroup");

		LLNI_field_set_ref(t, group, group);
  	}
# endif
#else
	/* we just return a fake java.lang.Thread object, otherwise we get
	   NullPointerException's in GNU classpath */

	t = (java_lang_Thread *) builtin_new(class_java_lang_Thread);
#endif

	return t;
}


/*
 * Class:     java/lang/Thread
 * Method:    yield
 * Signature: ()V
 */
void _Jv_java_lang_Thread_yield(void)
{
#if defined(ENABLE_THREADS)
	threads_yield();
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    interrupted
 * Signature: ()Z
 */
s4 _Jv_java_lang_Thread_interrupted(void)
{
#if defined(ENABLE_THREADS)
	return threads_check_if_interrupted_and_reset();
#else
	return 0;
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    holdsLock
 * Signature: (Ljava/lang/Object;)Z
 */
s4 _Jv_java_lang_Thread_holdsLock(java_lang_Object* obj)
{
#if defined(ENABLE_THREADS)
	java_handle_t *o;

	o = (java_handle_t *) obj;

	if (o == NULL) {
		exceptions_throw_nullpointerexception();
		return 0;
	}

	return lock_is_held_by_current_thread(o);
#else
	return 0;
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    getState
 * Signature: ()Ljava/lang/String;
 */
java_lang_String *_Jv_java_lang_Thread_getState(java_lang_Thread *this)
{
#if defined(ENABLE_THREADS)
	threadobject  *thread;
	utf           *u;
	java_handle_t *o;

# if defined(WITH_CLASSPATH_GNU)
	thread = (threadobject *) LLNI_field_direct(this, vmThread)->vmdata;
# elif defined(WITH_CLASSPATH_CLDC1_1)
	thread = (threadobject *) this->vm_thread;
# endif

	u = threads_thread_get_state(thread);
	o = javastring_new(u);

	return (java_lang_String *) o;
#else
	return NULL;
#endif
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
