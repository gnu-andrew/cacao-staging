/* nat/Thread.c - java/lang/Thread

   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   R. Grafl, A. Krall, C. Kruegel, C. Oates, R. Obermaisser,
   M. Probst, S. Ring, E. Steiner, C. Thalinger, D. Thuernbeck,
   P. Tomsich, J. Wenninger

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   Contact: cacao@complang.tuwien.ac.at

   Authors: Roman Obermaiser

   Changes: Joseph Wenninger

   $Id: VMThread.c 900 2004-01-22 13:25:36Z twisti $

*/


#include "jni.h"
#include "types.h"
#include "native.h"
#include "loader.h"
#include "tables.h"
#include "threads/thread.h"
#include "toolbox/loging.h"
#include "java_lang_ThreadGroup.h"
#include "java_lang_Object.h"         /* needed for java_lang_Thread.h */
#include "java_lang_Throwable.h"      /* needed for java_lang_Thread.h */
#include "java_lang_Thread.h"


/*
 * Class:     java/lang/Thread
 * Method:    countStackFrames
 * Signature: ()I
 */
JNIEXPORT s4 JNICALL Java_java_lang_Thread_countStackFrames(JNIEnv *env, java_lang_Thread *this)
{
    log_text("java_lang_Thread_countStackFrames called");

    return 0;
}

/*
 * Class:     java/lang/Thread
 * Method:    currentThread
 * Signature: ()Ljava/lang/Thread;
 */
JNIEXPORT java_lang_Thread* JNICALL Java_java_lang_Thread_currentThread(JNIEnv *env, jclass clazz)
{
	java_lang_Thread *t;

	if (runverbose)
		log_text("java_lang_Thread_currentThread called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	t = (java_lang_Thread *) currentThread;
  
	if (!t->group) {
		log_text("java_lang_Thread_currentThread: t->group=NULL");
		/* ThreadGroup of currentThread is not initialized */

		t->group = (java_lang_ThreadGroup *) 
			native_new_and_init(loader_load(utf_new_char("java/lang/ThreadGroup")));

		if (t->group == 0) 
			log_text("unable to create ThreadGroup");
  	}

	return (java_lang_Thread *) currentThread;
#else
	return 0;	
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    nativeInterrupt
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeInterrupt(JNIEnv *env, java_lang_Thread *this)
{
	log_text("Java_java_lang_Thread_interrupt0 called");
}


/*
 * Class:     java/lang/Thread
 * Method:    isAlive
 * Signature: ()Z
 */
JNIEXPORT s4 JNICALL Java_java_lang_Thread_isAlive(JNIEnv *env, java_lang_Thread *this)
{
	if (runverbose)
		log_text("java_lang_Thread_isAlive called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	return aliveThread((thread *) this);
#else
	return 0;
#endif
}



/*
 * Class:     java_lang_Thread
 * Method:    isInterrupted
 * Signature: ()Z
 */
JNIEXPORT s4 JNICALL Java_java_lang_Thread_isInterrupted(JNIEnv *env, java_lang_Thread *this)
{
	log_text("Java_java_lang_Thread_isInterrupted  called");
	return 0;
}


/*
 * Class:     java/lang/Thread
 * Method:    registerNatives
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_registerNatives(JNIEnv *env, jclass clazz)
{
	/* empty */
}


/*
 * Class:     java/lang/Thread
 * Method:    resume0
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeResume(JNIEnv *env, java_lang_Thread *this)
{
	if (runverbose)
		log_text("java_lang_Thread_resume0 called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	resumeThread((thread *) this);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    setPriority0
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeSetPriority(JNIEnv *env, java_lang_Thread *this, s4 par1)
{
    if (runverbose) 
		log_text("java_lang_Thread_setPriority0 called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	setPriorityThread((thread *) this, par1);
#endif
}


/*
 * Class:     java_lang_Thread
 * Method:    sleep
 * Signature: (JI)V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_sleep(JNIEnv *env, jclass clazz, s8 millis, s4 par2)
{
	if (runverbose)
		log_text("java_lang_Thread_sleep called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	sleepThread(millis);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    start
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_start(JNIEnv *env, java_lang_Thread *this)
{
	if (runverbose) 
		log_text("java_lang_Thread_start called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	startThread((thread*)this);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    stop0
 * Signature: (Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeStop(JNIEnv *env, java_lang_Thread *this, java_lang_Throwable *par1)
{
	if (runverbose)
		log_text ("java_lang_Thread_stop0 called");


#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	if (currentThread == (thread*)this) {
		log_text("killing");
		killThread(0);
		/*
		  exceptionptr = proto_java_lang_ThreadDeath;
		  return;
		*/

	} else {
		CONTEXT((thread*)this).flags |= THREAD_FLAGS_KILLED;
		resumeThread((thread*)this);
	}
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    suspend0
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeSuspend(JNIEnv *env, java_lang_Thread *this)
{
	if (runverbose)
		log_text("java_lang_Thread_suspend0 called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	suspendThread((thread*)this);
#endif
}


/*
 * Class:     java/lang/Thread
 * Method:    yield
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_yield(JNIEnv *env, jclass clazz)
{
	if (runverbose)
		log_text("java_lang_Thread_yield called");

#if defined(USE_THREADS) && !defined(NATIVE_THREADS)
	yieldThread();
#endif
}


/*
 * Class:     java_lang_Thread
 * Method:    interrupted
 * Signature: ()Z
 */
JNIEXPORT s4 JNICALL Java_java_lang_Thread_interrupted(JNIEnv *env, jclass clazz)
{
	log_text("Java_java_lang_Thread_interrupted");

	return 0;
}


/*
 * Class:     java_lang_Thread
 * Method:    nativeInit
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_java_lang_Thread_nativeInit(JNIEnv *env, java_lang_Thread *this, s8 par1)
{
	if (*exceptionptr)
		log_text("There has been an exception, strange...");

	this->priority = 5;
}


/*
 * Class:     java_lang_Thread
 * Method:    holdsLock
 * Signature: (Ljava/lang/Object;)Z
 */
JNIEXPORT s4 JNICALL Java_java_lang_Thread_holdsLock(JNIEnv *env, jclass clazz, java_lang_Object *par1)
{
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
