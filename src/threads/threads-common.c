/* src/threads/threads-common.c - machine independent thread functions

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
#include <unistd.h>

#include "vm/types.h"

#include "mm/memory.h"

#include "native/jni.h"
#include "native/llni.h"

#include "native/include/java_lang_Object.h"
#include "native/include/java_lang_String.h"
#include "native/include/java_lang_Thread.h"

#if defined(WITH_CLASSPATH_GNU)
# include "native/include/java_lang_Throwable.h"
# include "native/include/java_lang_VMThread.h"
#endif

#include "threads/critical.h"
#include "threads/lock-common.h"
#include "threads/threadlist.h"
#include "threads/threads-common.h"

#include "toolbox/list.h"

#include "vm/builtin.h"
#include "vm/stringlocal.h"
#include "vm/vm.h"

#include "vm/jit/stacktrace.h"

#include "vmcore/class.h"
#include "vmcore/options.h"

#if defined(ENABLE_STATISTICS)
# include "vmcore/statistics.h"
#endif

#include "vmcore/utf8.h"


/* global variables ***********************************************************/

#if defined(__LINUX__)
/* XXX Remove for exact-GC. */
bool threads_pthreads_implementation_nptl;
#endif


/* threads_preinit *************************************************************

   Do some early initialization of stuff required.

*******************************************************************************/

void threads_preinit(void)
{
	threadobject *mainthread;
#if defined(__LINUX__) && defined(_CS_GNU_LIBPTHREAD_VERSION)
	char         *pathbuf;
	size_t        len;
#endif

	TRACESUBSYSTEMINITIALIZATION("threads_preinit");

#if defined(__LINUX__)
	/* XXX Remove for exact-GC. */

	/* On Linux we need to check the pthread implementation. */

	/* _CS_GNU_LIBPTHREAD_VERSION (GNU C library only; since glibc 2.3.2) */
	/* If the glibc is a pre-2.3.2 version, we fall back to
	   linuxthreads. */

# if defined(_CS_GNU_LIBPTHREAD_VERSION)
	len = confstr(_CS_GNU_LIBPTHREAD_VERSION, NULL, (size_t) 0);

	/* Some systems return as length 0 (maybe cross-compilation
	   related).  In this case we also fall back to linuxthreads. */

	if (len > 0) {
		pathbuf = MNEW(char, len);

		(void) confstr(_CS_GNU_LIBPTHREAD_VERSION, pathbuf, len);

		if (strstr(pathbuf, "NPTL") != NULL)
			threads_pthreads_implementation_nptl = true;
		else
			threads_pthreads_implementation_nptl = false;
	}
	else
		threads_pthreads_implementation_nptl = false;
# else
	threads_pthreads_implementation_nptl = false;
# endif
#endif

	/* Initialize the threads implementation (sets the thinlock on the
	   main thread). */

	threads_impl_preinit();

	/* create internal thread data-structure for the main thread */

	mainthread = threads_thread_new();

	/* thread is a Java thread and running */

	mainthread->flags |= THREAD_FLAG_JAVA;
	mainthread->state = THREAD_STATE_RUNNABLE;

	/* store the internal thread data-structure in the TSD */

	threads_set_current_threadobject(mainthread);
}


/* threads_thread_new **********************************************************

   Allocates and initializes an internal thread data-structure and
   adds it to the threads list.

*******************************************************************************/

threadobject *threads_thread_new(void)
{
	int32_t         index;
	threadobject   *t;
	
	/* lock the threads-lists */

	threads_list_lock();

	index = threadlist_get_free_index();

	/* Allocate a thread data structure. */

	/* First, try to get one from the free-list. */

	t = threadlist_free_first();

	if (t != NULL) {
		/* Remove from free list. */

		threadlist_free_remove(t);

		/* Equivalent of MZERO on the else path */

		threads_impl_thread_clear(t);
	}
	else {
#if defined(ENABLE_GC_BOEHM)
		t = GCNEW_UNCOLLECTABLE(threadobject, 1);
#else
		t = NEW(threadobject);
#endif

#if defined(ENABLE_STATISTICS)
		if (opt_stat)
			size_threadobject += sizeof(threadobject);
#endif

		/* Clear memory. */

		MZERO(t, threadobject, 1);

#if defined(ENABLE_GC_CACAO)
		/* Register reference to java.lang.Thread with the GC. */
		/* FIXME is it ok to do this only once? */

		gc_reference_register(&(t->object), GC_REFTYPE_THREADOBJECT);
		gc_reference_register(&(t->_exceptionptr), GC_REFTYPE_THREADOBJECT);
#endif

		/* Initialize the implementation-specific bits. */

		threads_impl_thread_init(t);
	}

	/* Pre-compute the thinlock-word. */

	assert(index != 0);

	t->index     = index;
	t->thinlock  = lock_pre_compute_thinlock(t->index);
	t->flags     = 0;
	t->state     = THREAD_STATE_NEW;

#if defined(ENABLE_GC_CACAO)
	t->flags    |= THREAD_FLAG_IN_NATIVE; 
#endif

	/* Initialize the implementation-specific bits. */

	threads_impl_thread_reuse(t);

	/* Add the thread to the thread list. */

	threadlist_add(t);

	/* Unlock the threads-lists. */

	threads_list_unlock();

	return t;
}


/* threads_thread_free *********************************************************

   Remove the thread from the threads-list and free the internal
   thread data structure.  The thread index is added to the
   thread-index free-list.

   IN:
       t....thread data structure

*******************************************************************************/

void threads_thread_free(threadobject *t)
{
	/* Lock the threads lists. */

	threads_list_lock();

	/* Remove the thread from the thread-list. */

	threadlist_remove(t);

	/* Add the thread index to the free list. */

	threadlist_index_add(t->index);

	/* Add the thread data structure to the free list. */

	threads_thread_set_object(t, NULL);

	threadlist_free_add(t);

	/* Unlock the threads lists. */

	threads_list_unlock();
}


/* threads_thread_start_internal ***********************************************

   Start an internal thread in the JVM.  No Java thread objects exists
   so far.

   IN:
      name.......UTF-8 name of the thread
      f..........function pointer to C function to start

*******************************************************************************/

bool threads_thread_start_internal(utf *name, functionptr f)
{
	threadobject       *t;
	java_lang_Thread   *object;
#if defined(WITH_CLASSPATH_GNU)
	java_lang_VMThread *vmt;
#endif

	/* Enter the join-mutex, so if the main-thread is currently
	   waiting to join all threads, the number of non-daemon threads
	   is correct. */

	threads_mutex_join_lock();

	/* create internal thread data-structure */

	t = threads_thread_new();

	t->flags |= THREAD_FLAG_INTERNAL | THREAD_FLAG_DAEMON;

	/* The thread is flagged as (non-)daemon thread, we can leave the
	   mutex. */

	threads_mutex_join_unlock();

	/* create the java thread object */

	object = (java_lang_Thread *) builtin_new(class_java_lang_Thread);

	/* XXX memory leak!!! */
	if (object == NULL)
		return false;

#if defined(WITH_CLASSPATH_GNU)
	vmt = (java_lang_VMThread *) builtin_new(class_java_lang_VMThread);

	/* XXX memory leak!!! */
	if (vmt == NULL)
		return false;

	LLNI_field_set_ref(vmt, thread, object);
	LLNI_field_set_val(vmt, vmdata, (java_lang_Object *) t);

	LLNI_field_set_ref(object, vmThread, vmt);
#elif defined(WITH_CLASSPATH_CLDC1_1)
	LLNI_field_set_val(object, vm_thread, (java_lang_Object *) t);
#endif

	threads_thread_set_object(t, (java_handle_t *) object);

	/* set java.lang.Thread fields */

#if defined(WITH_CLASSPATH_GNU)
	LLNI_field_set_ref(object, name    , (java_lang_String *) javastring_new(name));
#elif defined(WITH_CLASSPATH_CLDC1_1)
	/* FIXME: In cldc the name is a char[] */
/* 	LLNI_field_set_ref(object, name    , (java_chararray *) javastring_new(name)); */
	LLNI_field_set_ref(object, name    , NULL);
#endif

#if defined(ENABLE_JAVASE)
	LLNI_field_set_val(object, daemon  , true);
#endif

	LLNI_field_set_val(object, priority, NORM_PRIORITY);

	/* start the thread */

	threads_impl_thread_start(t, f);

	/* everything's ok */

	return true;
}


/* threads_thread_start ********************************************************

   Start a Java thread in the JVM.  Only the java thread object exists
   so far.

   IN:
      object.....the java thread object java.lang.Thread

*******************************************************************************/

void threads_thread_start(java_handle_t *object)
{
	java_lang_Thread   *o;
	threadobject       *thread;
#if defined(WITH_CLASSPATH_GNU)
	java_lang_VMThread *vmt;
#endif

	o = (java_lang_Thread *) object;

	/* Enter the join-mutex, so if the main-thread is currently
	   waiting to join all threads, the number of non-daemon threads
	   is correct. */

	threads_mutex_join_lock();

	/* create internal thread data-structure */

	thread = threads_thread_new();

	/* this is a normal Java thread */

	thread->flags |= THREAD_FLAG_JAVA;

#if defined(ENABLE_JAVASE)
	/* is this a daemon thread? */

	if (LLNI_field_direct(o, daemon) == true)
		thread->flags |= THREAD_FLAG_DAEMON;
#endif

	/* The thread is flagged and (non-)daemon thread, we can leave the
	   mutex. */

	threads_mutex_join_unlock();

	/* link the two objects together */

	threads_thread_set_object(thread, object);

#if defined(WITH_CLASSPATH_GNU)
	LLNI_field_get_ref(o, vmThread, vmt);

	assert(vmt);
	assert(LLNI_field_direct(vmt, vmdata) == NULL);

	LLNI_field_set_val(vmt, vmdata, (java_lang_Object *) thread);
#elif defined(WITH_CLASSPATH_CLDC1_1)
	LLNI_field_set_val(o, vm_thread, (java_lang_Object *) thread);
#endif

	/* Start the thread.  Don't pass a function pointer (NULL) since
	   we want Thread.run()V here. */

	threads_impl_thread_start(thread, NULL);
}


/* threads_thread_print_info ***************************************************

   Print information of the passed thread.
   
*******************************************************************************/

void threads_thread_print_info(threadobject *t)
{
	java_lang_Thread *object;
#if defined(WITH_CLASSPATH_GNU)
	java_lang_String *namestring;
#endif
	utf              *name;

	assert(t->state != THREAD_STATE_NEW);

	/* the thread may be currently in initalization, don't print it */

	object = (java_lang_Thread *) threads_thread_get_object(t);

	if (object != NULL) {
		/* get thread name */

#if defined(WITH_CLASSPATH_GNU)
		LLNI_field_get_ref(object, name, namestring);
		name = javastring_toutf((java_handle_t *) namestring, false);
#elif defined(WITH_CLASSPATH_SUN) || defined(WITH_CLASSPATH_CLDC1_1)
		/* FIXME: In cldc the name is a char[] */
/* 		name = object->name; */
		name = utf_null;
#else
# error unknown classpath configuration
#endif

		printf("\"");
		utf_display_printable_ascii(name);
		printf("\"");

		if (t->flags & THREAD_FLAG_DAEMON)
			printf(" daemon");

		printf(" prio=%d", LLNI_field_direct(object, priority));

#if SIZEOF_VOID_P == 8
		printf(" t=0x%016lx tid=0x%016lx (%ld)",
			   (ptrint) t, (ptrint) t->tid, (ptrint) t->tid);
#else
		printf(" t=0x%08x tid=0x%08x (%d)",
			   (ptrint) t, (ptrint) t->tid, (ptrint) t->tid);
#endif

		printf(" index=%d", t->index);

		/* print thread state */

		switch (t->state) {
		case THREAD_STATE_NEW:
			printf(" new");
			break;
		case THREAD_STATE_RUNNABLE:
			printf(" runnable");
			break;
		case THREAD_STATE_BLOCKED:
			printf(" blocked");
			break;
		case THREAD_STATE_WAITING:
			printf(" waiting");
			break;
		case THREAD_STATE_TIMED_WAITING:
			printf(" waiting on condition");
			break;
		case THREAD_STATE_TERMINATED:
			printf(" terminated");
			break;
		default:
			vm_abort("threads_thread_print_info: unknown thread state %d",
					 t->state);
		}
	}
}


/* threads_get_current_tid *****************************************************

   Return the tid of the current thread.
   
   RETURN VALUE:
       the current tid

*******************************************************************************/

ptrint threads_get_current_tid(void)
{
	threadobject *thread;

	thread = THREADOBJECT;

	/* this may happen during bootstrap */

	if (thread == NULL)
		return 0;

	return (ptrint) thread->tid;
}


/* threads_get_current_object **************************************************

   Return the Java object of the current thread.
   
   RETURN VALUE:
       the Java object

*******************************************************************************/

#include "native/include/java_lang_ThreadGroup.h"

java_object_t *threads_get_current_object(void)
{
#if defined(ENABLE_THREADS)
	threadobject  *t;
# if defined(ENABLE_JAVASE)
	java_lang_ThreadGroup *group;
# endif
#endif
	java_lang_Thread *o;

#if defined(ENABLE_THREADS)
	t = THREADOBJECT;
	o = threads_thread_get_object(t);

# if defined(ENABLE_JAVASE)
	/* TODO Do we really need this code?  Or should we check, when we
	   create the threads, that all of them have a group? */
	/* TWISTI No, we don't need this code!  We need to allocate a
	   ThreadGroup before we initialize the main thread. */

	LLNI_field_get_ref(o, group, group);

	if (group == NULL) {
		/* ThreadGroup of currentThread is not initialized */

		group = (java_lang_ThreadGroup *)
			native_new_and_init(class_java_lang_ThreadGroup);

		if (group == NULL)
			vm_abort("unable to create ThreadGroup");

		LLNI_field_set_ref(o, group, group);
  	}
# endif
#else
	/* We just return a fake java.lang.Thread object, otherwise we get
	   NullPointerException's in GNU Classpath. */

	o = builtin_new(class_java_lang_Thread);
#endif

	return o;
}


/* threads_thread_state_runnable ***********************************************

   Set the current state of the given thread to THREAD_STATE_RUNNABLE.

   NOTE: If the thread has already terminated, don't set the state.
         This is important for threads_detach_thread.

*******************************************************************************/

void threads_thread_state_runnable(threadobject *t)
{
	/* Set the state inside a lock. */

	threads_list_lock();

	if (t->state != THREAD_STATE_TERMINATED)
		t->state = THREAD_STATE_RUNNABLE;

	DEBUGTHREADS("is RUNNABLE", t);

	threads_list_unlock();
}


/* threads_thread_state_waiting ************************************************

   Set the current state of the given thread to THREAD_STATE_WAITING.

   NOTE: If the thread has already terminated, don't set the state.
         This is important for threads_detach_thread.

*******************************************************************************/

void threads_thread_state_waiting(threadobject *t)
{
	/* Set the state inside a lock. */

	threads_list_lock();

	if (t->state != THREAD_STATE_TERMINATED)
		t->state = THREAD_STATE_WAITING;

	DEBUGTHREADS("is WAITING", t);

	threads_list_unlock();
}


/* threads_thread_state_timed_waiting ******************************************

   Set the current state of the given thread to
   THREAD_STATE_TIMED_WAITING.

   NOTE: If the thread has already terminated, don't set the state.
         This is important for threads_detach_thread.

*******************************************************************************/

void threads_thread_state_timed_waiting(threadobject *t)
{
	/* Set the state inside a lock. */

	threads_list_lock();

	if (t->state != THREAD_STATE_TERMINATED)
		t->state = THREAD_STATE_TIMED_WAITING;

	DEBUGTHREADS("is TIMED_WAITING", t);

	threads_list_unlock();
}


/* threads_thread_state_terminated *********************************************

   Set the current state of the given thread to
   THREAD_STATE_TERMINATED.

*******************************************************************************/

void threads_thread_state_terminated(threadobject *t)
{
	/* set the state in the lock */

	threads_list_lock();

	t->state = THREAD_STATE_TERMINATED;

	DEBUGTHREADS("is TERMINATED", t);

	threads_list_unlock();
}


/* threads_thread_get_state ****************************************************

   Returns the current state of the given thread.

*******************************************************************************/

utf *threads_thread_get_state(threadobject *t)
{
	utf *u;

	switch (t->state) {
	case THREAD_STATE_NEW:
		u = utf_new_char("NEW");
		break;
	case THREAD_STATE_RUNNABLE:
		u = utf_new_char("RUNNABLE");
		break;
	case THREAD_STATE_BLOCKED:
		u = utf_new_char("BLOCKED");
		break;
	case THREAD_STATE_WAITING:
		u = utf_new_char("WAITING");
		break;
	case THREAD_STATE_TIMED_WAITING:
		u = utf_new_char("TIMED_WAITING");
		break;
	case THREAD_STATE_TERMINATED:
		u = utf_new_char("TERMINATED");
		break;
	default:
		vm_abort("threads_get_state: unknown thread state %d", t->state);

		/* keep compiler happy */

		u = NULL;
	}

	return u;
}


/* threads_thread_is_alive *****************************************************

   Returns if the give thread is alive.

*******************************************************************************/

bool threads_thread_is_alive(threadobject *t)
{
	switch (t->state) {
	case THREAD_STATE_NEW:
	case THREAD_STATE_TERMINATED:
		return false;

	case THREAD_STATE_RUNNABLE:
	case THREAD_STATE_BLOCKED:
	case THREAD_STATE_WAITING:
	case THREAD_STATE_TIMED_WAITING:
		return true;

	default:
		vm_abort("threads_thread_is_alive: unknown thread state %d", t->state);
	}

	/* keep compiler happy */

	return false;
}


/* threads_dump ****************************************************************

   Dumps info for all threads running in the JVM.  This function is
   called when SIGQUIT (<ctrl>-\) is sent to CACAO.

*******************************************************************************/

void threads_dump(void)
{
	threadobject *t;

	/* XXX we should stop the world here */

	/* lock the threads lists */

	threads_list_lock();

	printf("Full thread dump CACAO "VERSION":\n");

	/* iterate over all started threads */

	for (t = threadlist_first(); t != NULL; t = threadlist_next(t)) {
		/* ignore threads which are in state NEW */
		if (t->state == THREAD_STATE_NEW)
			continue;

#if defined(ENABLE_GC_CACAO)
		/* Suspend the thread. */
		/* XXX Is the suspend reason correct? */

		if (threads_suspend_thread(t, SUSPEND_REASON_JNI) == false)
			vm_abort("threads_dump: threads_suspend_thread failed");
#endif

		/* Print thread info. */

		printf("\n");
		threads_thread_print_info(t);
		printf("\n");

		/* Print trace of thread. */

		threads_thread_print_stacktrace(t);

#if defined(ENABLE_GC_CACAO)
		/* Resume the thread. */

		if (threads_resume_thread(t) == false)
			vm_abort("threads_dump: threads_resume_thread failed");
#endif
	}

	/* unlock the threads lists */

	threads_list_unlock();
}


/* threads_thread_print_stacktrace *********************************************

   Print the current stacktrace of the given thread.

*******************************************************************************/

void threads_thread_print_stacktrace(threadobject *thread)
{
	stackframeinfo_t        *sfi;
	java_handle_bytearray_t *ba;
	stacktrace_t            *st;

	/* Build a stacktrace for the passed thread. */

	sfi = thread->_stackframeinfo;
	ba  = stacktrace_get(sfi);
	
	if (ba != NULL) {
		/* We need a critical section here as we use the byte-array
		   data pointer directly. */

		LLNI_CRITICAL_START;
	
		st = (stacktrace_t *) LLNI_array_data(ba);

		/* Print stacktrace. */

		stacktrace_print(st);

		LLNI_CRITICAL_END;
	}
	else {
		puts("\t<<No stacktrace available>>");
		fflush(stdout);
	}
}


/* threads_print_stacktrace ****************************************************

   Print the current stacktrace of the current thread.

*******************************************************************************/

void threads_print_stacktrace(void)
{
	threadobject *thread;

	thread = THREADOBJECT;

	threads_thread_print_stacktrace(thread);
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
