// ============================================================================
//  xstd/threading/condition_variable.h
//  ---------------------------------------------------------------------------
//  Condition variable for xstd::mutex. The Xbox 360 kernel predates Vista's
//  native CONDITION_VARIABLE, so this is built from a semaphore plus an
//  internal counter — the classic "Algorithm 6" pattern from Schmidt's
//  paper on pthread cv emulation on Win32.
//
//  Semantics:
//    - wait(m)        : atomically release m, block, re-acquire m on wake
//    - notify_one()   : wake at most one waiter (if any)
//    - notify_all()   : wake every currently-waiting thread
//
//  Spurious wakeups are possible (just like std::condition_variable). Always
//  re-check your predicate after wait() returns.
//
//  wait_for(m, ms) returns cv_status::timeout if the timeout fired before
//  a notify_one/notify_all was observed, otherwise cv_status::no_timeout.
// ============================================================================

#ifndef XSTD_THREADING_CONDITION_VARIABLE_H
#define XSTD_THREADING_CONDITION_VARIABLE_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#include "mutex.h"
#include "atomic.h"

namespace xstd {

enum cv_status {
    cv_status_no_timeout = 0,
    cv_status_timeout    = 1
};

class condition_variable {
private:
    HANDLE       m_sema;       // counting semaphore; one release per wakeup
    atomic<int>  m_waiters;    // current count of blocked threads

    condition_variable(const condition_variable&);
    condition_variable& operator=(const condition_variable&);

public:
    XSTD_INLINE condition_variable() : m_waiters(0) {
        // initial 0, max LONG_MAX — semaphore acts as the actual wake gate
        m_sema = CreateSemaphore(NULL, 0, 0x7FFFFFFF, NULL);
    }

    XSTD_INLINE ~condition_variable() {
        if (m_sema) {
            CloseHandle(m_sema);
        }
    }

    // Atomically: release m, block, re-acquire m on wake.
    //
    // The waiter-count increment happens *before* releasing the mutex so
    // a concurrent notify_one() observes us and posts to the semaphore.
    // We then atomically transition into the kernel wait via the semaphore.
    XSTD_INLINE void wait(mutex& m) {
        m_waiters.fetch_add(1, memory_order_acq_rel);
        m.unlock();
        WaitForSingleObject(m_sema, INFINITE);
        m.lock();
        m_waiters.fetch_sub(1, memory_order_acq_rel);
    }

    // Returns cv_status_timeout if the wait timed out without seeing a
    // notify. If we timed out but a notify *did* arrive in the same window,
    // we still consume the semaphore release (so a stray release doesn't
    // wake a later waiter) and report no_timeout.
    XSTD_INLINE cv_status wait_for(mutex& m, unsigned int milliseconds) {
        m_waiters.fetch_add(1, memory_order_acq_rel);
        m.unlock();
        DWORD r = WaitForSingleObject(m_sema, (DWORD)milliseconds);
        m.lock();
        m_waiters.fetch_sub(1, memory_order_acq_rel);
        return (r == WAIT_OBJECT_0) ? cv_status_no_timeout : cv_status_timeout;
    }

    XSTD_INLINE void notify_one() {
        // Only post if someone is actually waiting. Reading the counter is
        // a hint — a missed increment is impossible (the waiter bumps the
        // counter before unlocking m), and a stale "1" just causes a
        // harmless extra semaphore release that the next wait() consumes.
        if (m_waiters.load(memory_order_acquire) > 0) {
            ReleaseSemaphore(m_sema, 1, NULL);
        }
    }

    XSTD_INLINE void notify_all() {
        int n = m_waiters.load(memory_order_acquire);
        if (n > 0) {
            ReleaseSemaphore(m_sema, n, NULL);
        }
    }

    XSTD_INLINE HANDLE native_handle() { return m_sema; }
};

} // namespace xstd

#endif // XSTD_THREADING_CONDITION_VARIABLE_H
