// ============================================================================
//  xstd/threading/barrier.h
//  ---------------------------------------------------------------------------
//  Reusable rendezvous point. N threads each call arrive_and_wait(); the
//  N-th arrival releases all N and the barrier resets for the next round.
//
//  Uses a generation counter so spurious wakeups don't cause a thread from
//  generation G to leave on generation G+1's broadcast.
// ============================================================================

#ifndef XSTD_THREADING_BARRIER_H
#define XSTD_THREADING_BARRIER_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#include "mutex.h"
#include "condition_variable.h"

namespace xstd {

class barrier {
private:
    int                m_initial;
    int                m_count;
    int                m_generation;
    mutex              m_m;
    condition_variable m_cv;

    barrier(const barrier&);
    barrier& operator=(const barrier&);

public:
    XSTD_INLINE explicit barrier(int n)
        : m_initial(n), m_count(n), m_generation(0) {}

    XSTD_INLINE void arrive_and_wait() {
        m_m.lock();
        int my_gen = m_generation;
        --m_count;
        if (m_count == 0) {
            // last arrival — reset and release all
            ++m_generation;
            m_count = m_initial;
            m_m.unlock();
            m_cv.notify_all();
            return;
        }
        while (my_gen == m_generation) {
            m_cv.wait(m_m);
        }
        m_m.unlock();
    }
};

} // namespace xstd

#endif // XSTD_THREADING_BARRIER_H
