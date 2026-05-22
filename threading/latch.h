// ============================================================================
//  xstd/threading/latch.h
//  ---------------------------------------------------------------------------
//  Single-use countdown. Threads call count_down() to decrement, wait() to
//  block until the counter hits zero. Once at zero, the latch stays open
//  forever (no reset). Use barrier if you need reuse.
// ============================================================================

#ifndef XSTD_THREADING_LATCH_H
#define XSTD_THREADING_LATCH_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#include "mutex.h"
#include "condition_variable.h"
#include "atomic.h"

namespace xstd {

class latch {
private:
    atomic<int>         m_count;
    mutex               m_m;
    condition_variable  m_cv;

    latch(const latch&);
    latch& operator=(const latch&);

public:
    XSTD_INLINE explicit latch(int expected) : m_count(expected) {}

    XSTD_INLINE void count_down(int n = 1) {
        int prev = m_count.fetch_sub(n, memory_order_acq_rel);
        if (prev - n <= 0) {
            // grab+release the mutex to synchronize with sleeping waiters
            m_m.lock();
            m_m.unlock();
            m_cv.notify_all();
        }
    }

    XSTD_INLINE bool try_wait() const {
        return m_count.load(memory_order_acquire) <= 0;
    }

    XSTD_INLINE void wait() {
        if (try_wait()) return;
        m_m.lock();
        while (m_count.load(memory_order_acquire) > 0) {
            m_cv.wait(m_m);
        }
        m_m.unlock();
    }

    XSTD_INLINE void arrive_and_wait(int n = 1) {
        count_down(n);
        wait();
    }
};

} // namespace xstd

#endif // XSTD_THREADING_LATCH_H
