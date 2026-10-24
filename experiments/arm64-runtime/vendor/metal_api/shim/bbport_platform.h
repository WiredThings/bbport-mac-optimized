/* SPDX-License-Identifier: GPL-2.0-or-later
 * bbport: the few Linux interfaces the loader, runtime and GPU library use, for macOS. There
 * bbport is an x86-64 process run by Rosetta 2 (Apple Silicon): the game's code and this port's
 * code are translated together, signals carry x86-64 thread states, and pages are 4 KiB.
 * Included first into every source file (-include); on Linux it only defines BB_GREGS. */
#ifndef BBPORT_PLATFORM_H
#define BBPORT_PLATFORM_H

#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/qos.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
/* Only the Mach calls used here: <mach/mach.h> would declare thread_create and friends. */
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
/* <mach/vm_page_size.h> macros that collide with the port's own helpers. */
#undef round_page
#undef trunc_page
/* <ucontext.h> insists on _XOPEN_SOURCE for the deprecated makecontext family; the types come
 * from <sys/ucontext.h>, so the header itself is not needed. */
#define _UCONTEXT_H_

/* Thread state registers, indexed like Linux's gregs (the order of x86_thread_state64_t). */
enum {
    REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RDI, REG_RSI, REG_RBP, REG_RSP, REG_R8, REG_R9,
    REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RIP, REG_EFL, REG_CSGSFS
};
typedef long long greg_t;
#define BB_GREGS(uc) ((greg_t *)&(uc)->uc_mcontext->__ss)
/* Page fault error code (bit 1: a write). */
#if defined(__aarch64__)
#define BB_FAULT_ERR(uc) (((uc)->uc_mcontext->__es.__esr >> 5) & 2)
#else
#define BB_FAULT_ERR(uc) ((uc)->uc_mcontext->__es.__err)
#endif

static inline pid_t bb_gettid(void) {
    uint64_t id = 0;
    pthread_threadid_np(NULL, &id);
    return (pid_t)id;
}
#define gettid() bb_gettid()

/* CPU affinity: macOS schedules by quality of service, not by core masks. */
#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024
typedef struct { uint64_t bits[CPU_SETSIZE / 64]; } cpu_set_t;
#define CPU_ZERO(set) memset((set), 0, sizeof(cpu_set_t))
#define CPU_SET(cpu, set) ((set)->bits[(cpu) / 64] |= UINT64_C(1) << ((cpu) % 64))
#define CPU_ISSET(cpu, set) (((set)->bits[(cpu) / 64] >> ((cpu) % 64)) & 1)
static inline int bb_cpu_count(const cpu_set_t *set) {
    int n = 0;
    for (unsigned i = 0; i < CPU_SETSIZE / 64; ++i) n += __builtin_popcountll(set->bits[i]);
    return n;
}
#define CPU_COUNT(set) bb_cpu_count(set)
#endif
static inline int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set) {
    (void)pid; (void)size; (void)set; errno = ENOSYS; return -1;
}
static inline int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set) {
    (void)pid; (void)size; (void)set; return 0;
}
static inline int pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t *set) {
    (void)thread; (void)size; (void)set; return 0;
}
static inline int pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t *set) {
    (void)thread; (void)size; (void)set; return ENOSYS;
}
static inline int sched_getcpu(void) { return 0; }
/* Per-thread CPU clocks and usage of other threads: not offered (statistics only). */
static inline int pthread_getcpuclockid(pthread_t thread, clockid_t *clock) {
    (void)thread; (void)clock; return ENOSYS;
}
#define RUSAGE_THREAD 1 /* getrusage fails with EINVAL: no per-thread usage */
/* Background helpers (SCHED_IDLE on Linux) run at utility QoS: the efficiency cores first. */
#define SCHED_IDLE 5
#define SCHED_BATCH 3
static inline int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) {
    (void)pid; (void)param;
    if (policy != SCHED_IDLE && policy != SCHED_BATCH) return 0;
    return pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0) ? -1 : 0;
}

/* struct stat's timespec members under their POSIX names. */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec

#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif

static inline ssize_t getrandom(void *buffer, size_t size, unsigned flags) {
    (void)flags;
    arc4random_buf(buffer, size);
    return (ssize_t)size;
}

/* The calling thread's stack (base and size) as pthread_getattr_np reports it. */
static inline int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr) {
    const size_t size = pthread_get_stacksize_np(thread);
    char *top = (char *)pthread_get_stackaddr_np(thread);
    pthread_attr_init(attr);
    return pthread_attr_setstack(attr, top - size, size);
}

/* Clocks and timed waits macOS does not have. */
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif
static inline int64_t bb_timespec_ns(const struct timespec *t) {
    return (int64_t)t->tv_sec * 1000000000 + t->tv_nsec;
}
static inline int64_t bb_clock_ns(clockid_t clock) {
    struct timespec now;
    clock_gettime(clock, &now);
    return bb_timespec_ns(&now);
}
static inline int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request,
                                  struct timespec *remain) {
    int64_t ns = bb_timespec_ns(request);
    if (flags & TIMER_ABSTIME) ns -= bb_clock_ns(clock);
    if (ns <= 0) return 0;
    struct timespec wait = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
    return nanosleep(&wait, remain) ? errno : 0;
}
/* Timed locks: try until the CLOCK_REALTIME deadline, sleeping briefly in between. */
#define BB_TIMED_TRY(try_expression, deadline)                                                   \
    for (int64_t bb_end = bb_timespec_ns(deadline);;) {                                          \
        const int bb_e = (try_expression);                                                       \
        if (bb_e != EBUSY) return bb_e;                                                          \
        if (bb_clock_ns(CLOCK_REALTIME) >= bb_end) return ETIMEDOUT;                             \
        const struct timespec bb_pause = {0, 50000};                                             \
        nanosleep(&bb_pause, NULL);                                                              \
    }
static inline int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    BB_TIMED_TRY(pthread_mutex_trylock(mutex), deadline)
}
static inline int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    BB_TIMED_TRY(pthread_rwlock_tryrdlock(lock), deadline)
}
static inline int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    BB_TIMED_TRY(pthread_rwlock_trywrlock(lock), deadline)
}

/* pthread_setname_np(thread, name) as on Linux; macOS names only the calling thread. */
static inline int bb_setname1(const char *name) { return pthread_setname_np(name); }
static inline int bb_setname2(pthread_t thread, const char *name) {
    return pthread_equal(thread, pthread_self()) ? pthread_setname_np(name) : 0;
}
#define BB_PICK3(_1, _2, _3, name, ...) name
#define pthread_setname_np(...) BB_PICK3(__VA_ARGS__, bb_setname3_unsupported, bb_setname2, bb_setname1, 0)(__VA_ARGS__)

/* process_vm_readv on this process: a read that reports a fault instead of taking it. */
static inline ssize_t bb_process_vm_readv(pid_t pid, const struct iovec *local, unsigned long local_count,
                                          const struct iovec *remote, unsigned long remote_count,
                                          unsigned long flags) {
    (void)pid; (void)flags;
    if (local_count != 1 || remote_count != 1) { errno = EINVAL; return -1; }
    const size_t n = local->iov_len < remote->iov_len ? local->iov_len : remote->iov_len;
    mach_vm_size_t got = 0;
    if (mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)remote->iov_base, n,
                               (mach_vm_address_t)(uintptr_t)local->iov_base, &got) != KERN_SUCCESS) {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)got;
}
#define process_vm_readv bb_process_vm_readv

/* memfd_create: an unlinked POSIX shared memory object (sized once with ftruncate). */
#define MFD_CLOEXEC 1u
static inline int memfd_create(const char *name, unsigned flags) {
    char path[64];
    static int serial;
    snprintf(path, sizeof(path), "/bb-%d-%d-%.16s", (int)getpid(), __atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED),
             name ? name : "");
    for (char *c = path + 1; *c; ++c) if (*c == '/') *c = '-';
    const int fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(path);
        if (flags & MFD_CLOEXEC) fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return fd;
}

/* MAP_FIXED only works inside ranges this process reserved (macOS refuses fixed mappings in
 * unclaimed address space); MAP_FIXED_NOREPLACE is a hint that has to be met exactly. */
#define MAP_FIXED_NOREPLACE 0x40000000
static inline void *bb_mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    if (!(flags & MAP_FIXED_NOREPLACE)) return mmap(address, size, prot, flags, fd, offset);
    void *p = mmap(address, size, prot, flags & ~MAP_FIXED_NOREPLACE, fd, offset);
    if (p != MAP_FAILED && p != address) {
        munmap(p, size);
        errno = EEXIST;
        return MAP_FAILED;
    }
    return p;
}
#define mmap(address, size, prot, flags, fd, offset) bb_mmap(address, size, prot, flags, fd, offset)

#else /* Linux */
#define BB_GREGS(uc) ((uc)->uc_mcontext.gregs)
#define BB_FAULT_ERR(uc) ((uc)->uc_mcontext.gregs[REG_ERR])
#endif

#endif
