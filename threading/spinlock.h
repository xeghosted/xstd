// ============================================================================
//  xstd/threading/spinlock.h
//  ---------------------------------------------------------------------------
//  Lightweight busy-wait lock for very short critical sections. Built on
//  xstd::atomic_flag with acquire/release ordering. Yields the HW thread
//  via SwitchToThread after a small spin budget — Xenon has 2 HW threads
//  per core sharing the pipeline, so naked spinning starves the partner.
//
//  Use this only when you know the critical section is shorter than a
//  thread switch (~hundreds of cycles). Otherwise prefer xstd::mutex.
//
//  NOT recursive. Locking on a thread that already holds the lock deadlocks.
// ============================================================================

#ifndef XSTD_THREADING_SPINLOCK_H
#define XSTD_THREADING_SPINLOCK_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#include "atomic.h"

namespace xstd {

class spinlock {
private:
    atomic_flag m_flag;

    spinlock(const spinlock&);
    spinlock& operator=(const spinlock&);

public:
    XSTD_INLINE spinlock() {}

    XSTD_INLINE void lock() {
        // Spin-then-yield. The inner loop uses a relaxed read to keep the
        // cache line in shared state until we see clear, only then attempt
        // the test_and_set (test-and-test-and-set pattern).
        int spin_count = 0;
        while (m_flag.test_and_set(memory_order_acquire)) {
            do {
                ++spin_count;
                if ((spin_count & 0x3F) == 0) {
                    SwitchToThread();
                }
            } while (false);
        }
    }

    XSTD_INLINE bool try_lock() {
        return !m_flag.test_and_set(memory_order_acquire);
    }

    XSTD_INLINE void unlock() {
        m_flag.clear(memory_order_release);
    }
};

} // namespace xstd

#endif // XSTD_THREADING_SPINLOCK_H
