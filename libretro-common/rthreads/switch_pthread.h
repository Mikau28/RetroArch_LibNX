/* Copyright  (C) 2018 - M4xw <m4x@m4xw.net>, RetroArch Team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (switch_pthread.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _SWITCH_PTHREAD_WRAP_
#define _SWITCH_PTHREAD_WRAP_

#include <time.h>
#include <errno.h>
#include <stdio.h>

// memset
#include <string.h>

#include <retro_inline.h>
#include <switch.h>
#include "../../verbosity.h"

#define THREADVARS_MAGIC 0x21545624 // !TV$

extern unsigned cpu_features_get_core_amount(void);

// This structure is exactly 0x20 bytes, if more is needed modify getThreadVars() below
typedef struct
{
    // Magic value used to check if the struct is initialized
    u32 magic;

    // Thread handle, for mutexes
    Handle handle;

    // Pointer to the current thread (if exists)
    Thread *thread_ptr;

    // Pointer to this thread's newlib state
    struct _reent *reent;

    // Pointer to this thread's thread-local segment
    void *tls_tp; // !! Offset needs to be TLS+0x1F8 for __aarch64_read_tp !!
} ThreadVars;

static inline ThreadVars *getThreadVars(void)
{
    return (ThreadVars *)((u8 *)armGetTls() + 0x1E0);
}

#define STACKSIZE (8 * 1024)

/* libnx threads return void but pthreads return void pointer */
static bool mutex_inited = false;
static Mutex safe_double_thread_launch;
static void *(*start_routine_jump)(void *);

static INLINE void pthread_exit(void *retval)
{
    (void)retval;
    printf("[Threading]: Exiting Thread\n");
    svcExitThread();
}

// Access is safe by safe_double_thread_launch Mutex
static uint32_t threadCounter = 1;

static void switch_thread_launcher(void *data)
{
    threadCounter++;
    void *(*start_routine_jump_safe)(void *) = start_routine_jump;

    mutexUnlock(&safe_double_thread_launch);

    start_routine_jump_safe(data);

    return;
}

static INLINE int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    u32 prio = 0;
    Thread new_switch_thread;

    if (!mutex_inited)
    {
        mutexInit(&safe_double_thread_launch);
        mutex_inited = true;
    }

    mutexLock(&safe_double_thread_launch);

    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

    start_routine_jump = start_routine;
    //if (threadCounter == cpu_features_get_core_amount())
    //    threadCounter = 1;

    int rc = threadCreate(&new_switch_thread, switch_thread_launcher, arg, STACKSIZE, prio - 1, 1);

    if (R_FAILED(rc))
    {
        mutexUnlock(&safe_double_thread_launch);
        return EAGAIN;
    }

    printf("[Threading]: Starting Thread(#%i)\n", threadCounter);
    if (R_FAILED(threadStart(&new_switch_thread)))
    {
        threadClose(&new_switch_thread);
        mutexUnlock(&safe_double_thread_launch);
        return -1;
    }

    *thread = new_switch_thread;

    return 0;
}

Thread threadGetCurrent(void)
{
    ThreadVars *tv = getThreadVars();
    return *tv->thread_ptr;
}

static INLINE pthread_t pthread_self(void)
{
    return threadGetCurrent();
}

static INLINE int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    mutexInit(mutex);

    return 0;
}

static INLINE int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    // Nothing
    *mutex = 0;

    return 0;
}

static INLINE int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    mutexLock(mutex);
    return 0;
}

static INLINE int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    mutexUnlock(mutex);

    return 0;
}

static INLINE int pthread_detach(pthread_t thread)
{
    (void)thread;
    // Nothing for now
    return 0;
}

static INLINE int pthread_join(pthread_t thread, void **retval)
{
    printf("[Threading]: Waiting for Thread Exit\n");
    threadWaitForExit(&thread);
    threadClose(&thread);

    return 0;
}

static INLINE int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return mutexTryLock(mutex) ? 0 : 1;
}

static INLINE int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    cond->mutex = mutex;
    condvarWait(cond);

    return 0;
}

static INLINE int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    cond->mutex = mutex;
    condvarWaitTimeout(cond, abstime->tv_nsec);

    return 0;
}

static INLINE int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    condvarInit(cond, NULL);

    return 0;
}

static INLINE int pthread_cond_signal(pthread_cond_t *cond)
{
    condvarWakeOne(cond);
    return 0;
}

static INLINE int pthread_cond_broadcast(pthread_cond_t *cond)
{
    condvarWakeAll(cond);
    return 0;
}

static INLINE int pthread_cond_destroy(pthread_cond_t *cond)
{
    // Nothing
    return 0;
}

static INLINE int pthread_equal(pthread_t t1, pthread_t t2)
{
    if (t1.handle == t2.handle)
        return 1;

    return 0;
}

#endif
