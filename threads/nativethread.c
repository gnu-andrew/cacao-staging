#include "global.h"

#if defined(NATIVE_THREADS)

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "thread.h"
#include "codegen.h"
#include "locks.h"
#include "tables.h"
#include "native.h"
#include "loader.h"
#include "builtin.h"
#include "asmpart.h"
#include "exceptions.h"
#include "toolbox/logging.h"
#include "toolbox/memory.h"
#include "toolbox/avl.h"
#include "mm/boehm.h"

#include "nat/java_lang_Object.h"
#include "nat/java_lang_Throwable.h"
#include "nat/java_lang_Thread.h"
#include "nat/java_lang_ThreadGroup.h"

#include <pthread.h>
#include <semaphore.h>

#if !defined(__DARWIN__)
#if defined(__LINUX__)
#define GC_LINUX_THREADS
#elif defined(__MIPS__)
#define GC_IRIX_THREADS
#endif
#include "../mm/boehm-gc/include/gc.h"
#endif

#ifdef MUTEXSIM

typedef struct {
	pthread_mutex_t mutex;
	pthread_t owner;
	int count;
} pthread_mutex_rec_t;

static void pthread_mutex_init_rec(pthread_mutex_rec_t *m)
{
	pthread_mutex_init(&m->mutex, NULL);
	m->count = 0;
}

static void pthread_mutex_destroy_rec(pthread_mutex_rec_t *m)
{
	pthread_mutex_destroy(&m->mutex);
}

static void pthread_mutex_lock_rec(pthread_mutex_rec_t *m)
{
	for (;;)
		if (!m->count)
		{
			pthread_mutex_lock(&m->mutex);
			m->owner = pthread_self();
			m->count++;
			break;
		} else {
			if (m->owner != pthread_self())
				pthread_mutex_lock(&m->mutex);
			else
			{
				m->count++;
				break;
			}
		}
}

static void pthread_mutex_unlock_rec(pthread_mutex_rec_t *m)
{
	if (!--m->count)
		pthread_mutex_unlock(&m->mutex);
}

#else /* MUTEXSIM */

#define pthread_mutex_lock_rec pthread_mutex_lock
#define pthread_mutex_unlock_rec pthread_mutex_unlock
#define pthread_mutex_rec_t pthread_mutex_t

#endif /* MUTEXSIM */

#include "machine-instr.h"

static struct avl_table *criticaltree;
static threadobject *mainthreadobj;

#ifndef HAVE___THREAD
pthread_key_t tkey_threadinfo;
#else
__thread threadobject *threadobj;
#endif

static pthread_mutex_rec_t compiler_mutex;
static pthread_mutex_rec_t tablelock;

void compiler_lock()
{
	pthread_mutex_lock_rec(&compiler_mutex);
}

void compiler_unlock()
{
	pthread_mutex_unlock_rec(&compiler_mutex);
}

void tables_lock()
{
    pthread_mutex_lock_rec(&tablelock);
}

void tables_unlock()
{
    pthread_mutex_unlock_rec(&tablelock);
}

static int criticalcompare(const void *pa, const void *pb, void *param)
{
	const threadcritnode *na = pa;
	const threadcritnode *nb = pb;

	if (na->mcodebegin < nb->mcodebegin)
		return -1;
	if (na->mcodebegin > nb->mcodebegin)
		return 1;
	return 0;
}

static const threadcritnode *findcritical(u1 *mcodeptr)
{
    struct avl_node *n = criticaltree->avl_root;
    const threadcritnode *m = NULL;
    if (!n)
        return NULL;
    for (;;)
    {
        const threadcritnode *d = n->avl_data;
        if (mcodeptr == d->mcodebegin)
            return d;
        if (mcodeptr < d->mcodebegin) {
            if (n->avl_link[0])
                n = n->avl_link[0];
            else
                return m;
        } else {
            if (n->avl_link[1]) {
                m = n->avl_data;
                n = n->avl_link[1];
            } else
                return n->avl_data;
        }
    }
}

void thread_registercritical(threadcritnode *n)
{
	avl_insert(criticaltree, n);
}

u1 *thread_checkcritical(u1 *mcodeptr)
{
	const threadcritnode *n = findcritical(mcodeptr);
	return (n && mcodeptr < n->mcodeend && mcodeptr > n->mcodebegin) ? n->mcoderestart : NULL;
}

static void thread_addstaticcritical()
{
	threadcritnode *n = &asm_criticalsections;

	while (n->mcodebegin)
		thread_registercritical(n++);
}

static pthread_mutex_t threadlistlock;

static pthread_mutex_t stopworldlock;
volatile int stopworldwhere;

static sem_t suspend_ack;
#if defined(__MIPS__)
static pthread_mutex_t suspend_ack_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t suspend_cond = PTHREAD_COND_INITIALIZER;
#endif

/*
 * where - 1 from within GC
           2 class numbering
 */
void lock_stopworld(int where)
{
	pthread_mutex_lock(&stopworldlock);
	stopworldwhere = where;
}

void unlock_stopworld()
{
	stopworldwhere = 0;
	pthread_mutex_unlock(&stopworldlock);
}

#if !defined(__DARWIN__)
/* Caller must hold threadlistlock */
static int cast_sendsignals(int sig, int count)
{
	/* Count threads */
	threadobject *tobj = mainthreadobj;
	nativethread *infoself = THREADINFO;

	if (count == 0)
		do {
			count++;
			tobj = tobj->info.next;
		} while (tobj != mainthreadobj);

	do {
		nativethread *info = &tobj->info;
		if (info != infoself)
			pthread_kill(info->tid, sig);
		tobj = tobj->info.next;
	} while (tobj != mainthreadobj);

	return count-1;
}

#else

static void cast_darwinstop()
{
	threadobject *tobj = mainthreadobj;
	nativethread *infoself = THREADINFO;

	do {
		nativethread *info = &tobj->info;
		if (info != infoself)
		{
			thread_state_flavor_t flavor = PPC_THREAD_STATE;
			mach_msg_type_number_t thread_state_count = PPC_THREAD_STATE_COUNT;
			ppc_thread_state_t thread_state;
			mach_port_t thread = info->mach_thread;
			kern_return_t r;

			r = thread_suspend(thread);
			if (r != KERN_SUCCESS)
				panic("thread_suspend failed");

			r = thread_get_state(thread, flavor,
				(natural_t*)&thread_state, &thread_state_count);
			if (r != KERN_SUCCESS)
				panic("thread_get_state failed");

			thread_restartcriticalsection(&thread_state);

			r = thread_set_state(thread, flavor,
				(natural_t*)&thread_state, thread_state_count);
			if (r != KERN_SUCCESS)
				panic("thread_set_state failed");
		}
		tobj = tobj->info.next;
	} while (tobj != mainthreadobj);
}

static void cast_darwinresume()
{
	threadobject *tobj = mainthreadobj;
	nativethread *infoself = THREADINFO;

	do {
		nativethread *info = &tobj->info;
		if (info != infoself)
		{
			r = thread_resume(thread);
			if (r != KERN_SUCCESS)
				panic("thread_resume failed");
		}
		tobj = tobj->info.next;
	} while (tobj != mainthreadobj);
}

#endif

#if defined(__MIPS__)
static void cast_irixresume()
{
	pthread_mutex_lock(&suspend_ack_lock);
	pthread_cond_broadcast(&suspend_cond);
	pthread_mutex_unlock(&suspend_ack_lock);
}
#endif

void cast_stopworld()
{
	int count, i;
	lock_stopworld(2);
	pthread_mutex_lock(&threadlistlock);
#if defined(__DARWIN__)
	cast_darwinstop();
#else
	count = cast_sendsignals(GC_signum1(), 0);
	for (i=0; i<count; i++)
		sem_wait(&suspend_ack);
#endif
	pthread_mutex_unlock(&threadlistlock);
}

void cast_startworld()
{
	pthread_mutex_lock(&threadlistlock);
#if defined(__DARWIN__)
	cast_darwinresume();
#elif defined(__MIPS__)
	cast_irixresume();
#else
	cast_sendsignals(GC_signum2(), -1);
#endif
	pthread_mutex_unlock(&threadlistlock);
	unlock_stopworld();
}

#if !defined(__DARWIN__)
static void sigsuspend_handler(ucontext_t *ctx)
{
	int sig;
	sigset_t sigs;
	
	thread_restartcriticalsection(ctx);

	/* Do as Boehm does. On IRIX a condition variable is used for wake-up
	   (not POSIX async-safe). */
#if defined(__MIPS__)
	pthread_mutex_lock(&suspend_ack_lock);
	sem_post(&suspend_ack);
	pthread_cond_wait(&suspend_cond, &suspend_ack_lock);
	pthread_mutex_unlock(&suspend_ack_lock);
#else
	sem_post(&suspend_ack);

	sig = GC_signum2();
	sigfillset(&sigs);
	sigdelset(&sigs, sig);
	sigsuspend(&sigs);
#endif
}

int cacao_suspendhandler(ucontext_t *ctx)
{
	if (stopworldwhere != 2)
		return 0;

	sigsuspend_handler(ctx);
	return 1;
}
#endif

static void setthreadobject(threadobject *thread)
{
#if !defined(HAVE___THREAD)
	pthread_setspecific(tkey_threadinfo, thread);
#else
	threadobj = thread;
#endif
}

/*
 * Initialize threads.
 */
void
initThreadsEarly()
{
#ifndef MUTEXSIM
	pthread_mutexattr_t mutexattr;
	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&compiler_mutex, &mutexattr);
	pthread_mutex_init(&tablelock, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);
#else
	pthread_mutex_init_rec(&compiler_mutex);
	pthread_mutex_init_rec(&tablelock);
#endif

	pthread_mutex_init(&threadlistlock, NULL);
	pthread_mutex_init(&stopworldlock, NULL);

	/* Allocate something so the garbage collector's signal handlers are  */
	/* installed. */
	heap_allocate(1, false, NULL);

	mainthreadobj = NEW(threadobject);
	memset(mainthreadobj, 0, sizeof(threadobject));
#if !defined(HAVE___THREAD)
	pthread_key_create(&tkey_threadinfo, NULL);
#endif
	setthreadobject(mainthreadobj);

    criticaltree = avl_create(criticalcompare, NULL, NULL);
	thread_addstaticcritical();
	sem_init(&suspend_ack, 0, 0);
}

static pthread_attr_t threadattr;
static void freeLockRecordPools(lockRecordPool *);

void
initThreads(u1 *stackbottom)
{
	classinfo *threadclass;
	classinfo *threadgroupclass;
	java_lang_Thread *mainthread;
	threadobject *tempthread = mainthreadobj;

	threadclass = class_new(utf_new_char("java/lang/Thread"));
	class_load(threadclass);
	class_link(threadclass);

	if (!threadclass)
		throw_exception_exit();

	freeLockRecordPools(mainthreadobj->ee.lrpool);
	threadclass->instancesize = sizeof(threadobject);

	mainthreadobj = (threadobject *) builtin_new(threadclass);

	if (!mainthreadobj)
		throw_exception_exit();

	FREE(tempthread, threadobject);
	initThread(&mainthreadobj->o);

#if !defined(HAVE___THREAD)
	pthread_setspecific(tkey_threadinfo, mainthreadobj);
#else
	threadobj = mainthreadobj;
#endif

	mainthread = &mainthreadobj->o;
	initLocks();
	mainthreadobj->info.next = mainthreadobj;
	mainthreadobj->info.prev = mainthreadobj;

	mainthread->name=javastring_new(utf_new_char("main"));

	/* Allocate and init ThreadGroup */
	threadgroupclass = class_new(utf_new_char("java/lang/ThreadGroup"));
	mainthread->group =
		(java_lang_ThreadGroup *) native_new_and_init(threadgroupclass);

	if (!mainthread->group)
		throw_exception_exit();

	pthread_attr_init(&threadattr);
	pthread_attr_setdetachstate(&threadattr, PTHREAD_CREATE_DETACHED);
}

void initThread(java_lang_Thread *t)
{
	nativethread *info = &((threadobject*) t)->info;
	pthread_mutex_init(&info->joinMutex, NULL);
	pthread_cond_init(&info->joinCond, NULL);
}

static void initThreadLocks(threadobject *);

typedef struct {
	threadobject *thread;
	sem_t *psem;
} startupinfo;

static void *threadstartup(void *t)
{
	startupinfo *startup = t;
	threadobject *thread = startup->thread;
	sem_t *psem = startup->psem;
	nativethread *info = &thread->info;
	threadobject *tnext;
	methodinfo *method;

	t = NULL;
#if defined(__DARWIN__)
	info->mach_thread = mach_thread_self();
#endif
	setthreadobject(thread);

	pthread_mutex_lock(&threadlistlock);
	info->prev = mainthreadobj;
	info->next = tnext = mainthreadobj->info.next;
	mainthreadobj->info.next = thread;
	tnext->info.prev = thread;
	pthread_mutex_unlock(&threadlistlock);

	initThreadLocks(thread);

	startup = NULL;
	sem_post(psem);

	/* Find the run()V method and call it */
	method = class_resolveclassmethod(thread->o.header.vftbl->class,
									  utf_new_char("run"),
									  utf_new_char("()V"),
									  thread->o.header.vftbl->class,
									  true);

	/* if method != NULL, we had not exception */
	if (method) {
		asm_calljavafunction(method, thread, NULL, NULL, NULL);

	} else {
		throw_exception();
	}

	freeLockRecordPools(thread->ee.lrpool);

	pthread_mutex_lock(&threadlistlock);
	info->next->info.prev = info->prev;
	info->prev->info.next = info->next;
	pthread_mutex_unlock(&threadlistlock);

	pthread_mutex_lock(&info->joinMutex);
	info->tid = 0;
	pthread_mutex_unlock(&info->joinMutex);
	pthread_cond_broadcast(&info->joinCond);

	return NULL;
}

void startThread(threadobject *t)
{
	nativethread *info = &t->info;
	sem_t sem;
	startupinfo startup;

	startup.thread = t;
	startup.psem = &sem;

	sem_init(&sem, 0, 0);
	
	if (pthread_create(&info->tid, &threadattr, threadstartup, &startup))
		panic("pthread_create failed");

	/* Wait here until thread has entered itself into the thread list */
	sem_wait(&sem);
	sem_destroy(&sem);
}

void joinAllThreads()
{
	pthread_mutex_lock(&threadlistlock);
	while (mainthreadobj->info.prev != mainthreadobj) {
		nativethread *info = &mainthreadobj->info.prev->info;
		pthread_mutex_lock(&info->joinMutex);
		pthread_mutex_unlock(&threadlistlock);
		if (info->tid)
			pthread_cond_wait(&info->joinCond, &info->joinMutex);
		pthread_mutex_unlock(&info->joinMutex);
		pthread_mutex_lock(&threadlistlock);
	}
	pthread_mutex_unlock(&threadlistlock);
}

bool aliveThread(java_lang_Thread *t)
{
	return ((threadobject*) t)->info.tid != 0;
}

void sleepThread(s8 millis, s4 nanos)
{
	struct timespec tv;
	tv.tv_sec = millis / 1000;
	tv.tv_nsec = millis % 1000 * 1000000 + nanos;
	do { } while (nanosleep(&tv, &tv) == EINTR);
}

void yieldThread()
{
	sched_yield();
}

static void timedCondWait(pthread_cond_t *cond, pthread_mutex_t *mutex, s8 millis)
{
	struct timeval now;
	struct timespec desttime;
	gettimeofday(&now, NULL);
	desttime.tv_sec = millis / 1000;
	desttime.tv_nsec = millis % 1000 * 1000000;
	pthread_cond_timedwait(cond, mutex, &desttime);
}


#define NEUTRAL 0
#define LOCKED 1
#define WAITERS 2
#define BUSY 3

static void initExecutionEnvironment(ExecEnvironment *ee)
{
	pthread_mutex_init(&ee->metaLockMutex, NULL);
	pthread_cond_init(&ee->metaLockCond, NULL);
	pthread_mutex_init(&ee->monitorLockMutex, NULL);
	pthread_cond_init(&ee->monitorLockCond, NULL);
}

static void initLockRecord(monitorLockRecord *r, threadobject *t)
{
	r->owner = t;
	r->lockCount = 1;
	r->queue = NULL;
}

void initLocks()
{
	initThreadLocks(mainthreadobj);
}

static void initThreadLocks(threadobject *thread)
{
	int i;

	initExecutionEnvironment(&thread->ee);
	for (i=0; i<INITIALLOCKRECORDS; i++) {
		monitorLockRecord *r = &thread->ee.lr[i];
		initLockRecord(r, thread);
		r->nextFree = &thread->ee.lr[i+1];
	}
	thread->ee.lr[i-1].nextFree = NULL;
	thread->ee.firstLR = &thread->ee.lr[0];
}

static inline int lockState(long r)
{
	return (int) r & 3;
}

static inline void *lockRecord(long r)
{
	return (void*) (r & ~3L);
}

static inline long makeLockBits(void *r, long l)
{
	return ((long) r) | l;
}

static lockRecordPool *allocLockRecordPool(threadobject *thread, int size)
{
	lockRecordPool *p = mem_alloc(sizeof(lockRecordPoolHeader) + sizeof(monitorLockRecord) * size);
	int i;

	p->header.size = size;
	for (i=0; i<size; i++) {
		initLockRecord(&p->lr[i], thread);
		p->lr[i].nextFree = &p->lr[i+1];
	}
	p->lr[i-1].nextFree = NULL;
	return p;
}

static void freeLockRecordPools(lockRecordPool *pool)
{
	while (pool) {
		lockRecordPool *n = pool->header.next;
		mem_free(pool, sizeof(lockRecordPoolHeader) + sizeof(monitorLockRecord) * pool->header.size);
		pool = n;
	}
}

static monitorLockRecord *allocLockRecord(threadobject *t)
{
	monitorLockRecord *r = t->ee.firstLR;

	if (!r) {
		int poolsize = t->ee.lrpool ? t->ee.lrpool->header.size * 2 : INITIALLOCKRECORDS * 2;
		lockRecordPool *pool = allocLockRecordPool(t, poolsize);
		pool->header.next = t->ee.lrpool;
		t->ee.lrpool = pool;
		r = &pool->lr[0];
	}
	
	t->ee.firstLR = r->nextFree;
	return r;
}

static void recycleLockRecord(threadobject *t, monitorLockRecord *r)
{
	r->nextFree = t->ee.firstLR;
	t->ee.firstLR = r;
}

static monitorLockRecord *appendToQueue(monitorLockRecord *queue, monitorLockRecord *lr)
{
	monitorLockRecord *queuestart = queue;
	if (!queue)
		return lr;
	while (queue->queue)
		queue = queue->queue;
	queue->queue = lr;
	return queuestart;
}

static monitorLockRecord *moveMyLRToFront(threadobject *t, monitorLockRecord *lr)
{
	monitorLockRecord *pred = NULL;
	monitorLockRecord *firstLR = lr;
	while (lr->owner != t) {
		pred = lr;
		lr = lr->queue;
	}
	if (!pred)
		return lr;
	pred->queue = lr->queue;
	lr->queue = firstLR;
	lr->storedBits = firstLR->storedBits;
	return lr;
}

static long getMetaLockSlow(threadobject *t, long predBits);
static void releaseMetaLockSlow(threadobject *t, long releaseBits);

static long getMetaLock(threadobject *t, java_objectheader *o)
{
	long busyBits = makeLockBits(t, BUSY);
	long lockBits = atomic_swap(&o->monitorBits, busyBits);
	return lockState(lockBits) != BUSY ? lockBits : getMetaLockSlow(t, lockBits);
}

static long getMetaLockSlow(threadobject *t, long predBits)
{
	long bits;
	threadobject *pred = lockRecord(predBits);
	pthread_mutex_lock(&pred->ee.metaLockMutex);
	if (!pred->ee.bitsForGrab) {
		pred->ee.succ = t;
		do {
			pthread_cond_wait(&pred->ee.metaLockCond, &pred->ee.metaLockMutex);
		} while (!t->ee.gotMetaLockSlow);
		t->ee.gotMetaLockSlow = false;
		bits = t->ee.metaLockBits;
	} else {
		bits = pred->ee.metaLockBits;
		pred->ee.bitsForGrab = false;
		pthread_cond_signal(&pred->ee.metaLockCond);
	}
	pthread_mutex_unlock(&pred->ee.metaLockMutex);
	return bits;
}

static void releaseMetaLock(threadobject *t, java_objectheader *o, long releaseBits)
{
	long busyBits = makeLockBits(t, BUSY);
	int locked = compare_and_swap(&o->monitorBits, busyBits, releaseBits) != 0;
	
	if (!locked)
		releaseMetaLockSlow(t, releaseBits);
}

static void releaseMetaLockSlow(threadobject *t, long releaseBits)
{
	pthread_mutex_lock(&t->ee.metaLockMutex);
	if (t->ee.succ) {
		assert(!t->ee.succ->ee.bitsForGrab);
		assert(!t->ee.bitsForGrab);
		assert(!t->ee.succ->ee.gotMetaLockSlow);
		t->ee.succ->ee.metaLockBits = releaseBits;
		t->ee.succ->ee.gotMetaLockSlow = true;
		t->ee.succ = NULL;
		pthread_cond_signal(&t->ee.metaLockCond);
	} else {
		t->ee.metaLockBits = releaseBits;
		t->ee.bitsForGrab = true;
		do {
			pthread_cond_wait(&t->ee.metaLockCond, &t->ee.metaLockMutex);
		} while (t->ee.bitsForGrab);
	}
	pthread_mutex_unlock(&t->ee.metaLockMutex);
}

static void monitorEnterSlow(threadobject *t, java_objectheader *o, long r);

void monitorEnter(threadobject *t, java_objectheader *o)
{
	long r = getMetaLock(t, o);
	int state = lockState(r);

	if (state == NEUTRAL) {
		monitorLockRecord *lr = allocLockRecord(t);
		lr->storedBits = r;
		releaseMetaLock(t, o, makeLockBits(lr, LOCKED));
	} else if (state == LOCKED) {
		monitorLockRecord *ownerLR = lockRecord(r);
		if (ownerLR->owner == t) {
			ownerLR->lockCount++;
			releaseMetaLock(t, o, r);
		} else {
			monitorLockRecord *lr = allocLockRecord(t);
			ownerLR->queue = appendToQueue(ownerLR->queue, lr);
			monitorEnterSlow(t, o, r);
		}
	} else if (state == WAITERS) {
		monitorLockRecord *lr = allocLockRecord(t);
		monitorLockRecord *firstWaiterLR = lockRecord(r);
		lr->queue = firstWaiterLR;
		lr->storedBits = firstWaiterLR->storedBits;
		releaseMetaLock(t, o, makeLockBits(lr, LOCKED));
	}
}

static void monitorEnterSlow(threadobject *t, java_objectheader *o, long r)
{
	monitorLockRecord *lr;
	while (lockState(r) == LOCKED) {
		pthread_mutex_lock(&t->ee.monitorLockMutex);
		releaseMetaLock(t, o, r);
		pthread_cond_wait(&t->ee.monitorLockCond, &t->ee.monitorLockMutex);
		pthread_mutex_unlock(&t->ee.monitorLockMutex);
		r = getMetaLock(t, o);
	}
	assert(lockState(r) == WAITERS);
	lr = moveMyLRToFront(t, lockRecord(r));
	releaseMetaLock(t, o, makeLockBits(lr, LOCKED));
}

static void monitorExitSlow(threadobject *t, java_objectheader *o, monitorLockRecord *lr);

void monitorExit(threadobject *t, java_objectheader *o)
{
	long r = getMetaLock(t, o);
	monitorLockRecord *ownerLR = lockRecord(r);
	int state = lockState(r);

	if (state == LOCKED && ownerLR->owner == t) {
		assert(ownerLR->lockCount >= 1);
		if (ownerLR->lockCount == 1) {
			if (ownerLR->queue == NULL) {
				assert(lockState(ownerLR->storedBits) == NEUTRAL);
				releaseMetaLock(t, o, ownerLR->storedBits);
			} else {
				ownerLR->queue->storedBits = ownerLR->storedBits;
				monitorExitSlow(t, o, ownerLR->queue);
				ownerLR->queue = NULL;
			}
			recycleLockRecord(t, ownerLR);
		} else {
			ownerLR->lockCount--;
			releaseMetaLock(t, o, r);
		}

	} else {
		releaseMetaLock(t, o, r);

		/* throw an exception */

		*exceptionptr =
			new_exception(string_java_lang_IllegalMonitorStateException);
	}
}

static threadobject *wakeupEE(monitorLockRecord *lr)
{
	while (lr->queue && lr->queue->owner->ee.isWaitingForNotify)
		lr = lr->queue;
	return lr->owner;
}

static void monitorExitSlow(threadobject *t, java_objectheader *o, monitorLockRecord *lr)
{
	threadobject *wakeEE = wakeupEE(lr);
	if (wakeEE) {
		pthread_mutex_lock(&wakeEE->ee.monitorLockMutex);
		releaseMetaLock(t, o, makeLockBits(lr, WAITERS));
		pthread_cond_signal(&wakeEE->ee.monitorLockCond);
		pthread_mutex_unlock(&wakeEE->ee.monitorLockMutex);
	} else {
		releaseMetaLock(t, o, makeLockBits(lr, WAITERS));
	}
}

void monitorWait(threadobject *t, java_objectheader *o, s8 millis)
{
	long r = getMetaLock(t, o);
	monitorLockRecord *ownerLR = lockRecord(r);
	int state = lockState(r);

	if (state == LOCKED && ownerLR->owner == t) {
		pthread_mutex_lock(&t->ee.monitorLockMutex);
		t->ee.isWaitingForNotify = true;
		monitorExitSlow(t, o, ownerLR);
		if (millis == -1)
			pthread_cond_wait(&t->ee.monitorLockCond, &t->ee.monitorLockMutex);
		else
			timedCondWait(&t->ee.monitorLockCond, &t->ee.monitorLockMutex, millis);
		t->ee.isWaitingForNotify = false;
		pthread_mutex_unlock(&t->ee.monitorLockMutex);
		r = getMetaLock(t, o);
		monitorEnterSlow(t, o, r);

	} else {
		releaseMetaLock(t, o, r);

		/* throw an exception */

		*exceptionptr =
			new_exception(string_java_lang_IllegalMonitorStateException);
	}
}

static void notifyOneOrAll(threadobject *t, java_objectheader *o, bool one)
{
	long r = getMetaLock(t, o);
	monitorLockRecord *ownerLR = lockRecord(r);
	int state = lockState(r);

	if (state == LOCKED && ownerLR->owner == t) {
		monitorLockRecord *q = ownerLR->queue;
		while (q) {
			if (q->owner->ee.isWaitingForNotify) {
				q->owner->ee.isWaitingForNotify = false;
				if (one)
					break;
			}
			q = q->queue;
		}
		releaseMetaLock(t, o, r);

	} else {
		releaseMetaLock(t, o, r);

		/* throw an exception */

		*exceptionptr =
			new_exception(string_java_lang_IllegalMonitorStateException);
	}
}

void wait_cond_for_object(java_objectheader *o, s8 time)
{
	threadobject *t = (threadobject*) THREADOBJECT;
	monitorWait(t, o, time);
}

void signal_cond_for_object(java_objectheader *o)
{
	threadobject *t = (threadobject*) THREADOBJECT;
	notifyOneOrAll(t, o, true);
}

void broadcast_cond_for_object(java_objectheader *o)
{
	threadobject *t = (threadobject*) THREADOBJECT;
	notifyOneOrAll(t, o, false);
}

#endif


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
