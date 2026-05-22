// ============================================================================
//  xstd/threading/shared_mutex.h
//  ---------------------------------------------------------------------------
//  Reader/writer lock. Xbox 360 kernel predates Win32 SRWLOCK, so this is
//  built on top of xstd::mutex + xstd::condition_variable.
//
//  Semantics (matches std::shared_mutex):
//    - lock()          : exclusive write lock; blocks until no readers/writer
//    - try_lock()
//    - unlock()
//    - lock_shared()   : shared read lock; multiple readers allowed
//    - try_lock_shared()
//    - unlock_shared()
//
//  State integer:
//      0   no holders
//     >0   N readers active
//     -1   one writer active
//
//  Policy: writer-preference. Once a writer is waiting, new readers block.
//  Prevents writer starvation in reader-heavy workloads — the common case
//  for this primitive.
// ============================================================================

#ifndef XSTD_THREADING_SHARED_MUTEX_H
#define XSTD_THREADING_SHARED_MUTEX_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#include "mutex.h"
#include "condition_variable.h"
#include "atomic.h"

namespace xstd {

class shared_mutex {
private:
    mutex                m_gate;
    condition_variable   m_cv;
    int                  m_state;          // 0, >0 readers, -1 writer
    int                  m_writers_waiting;

    shared_mutex(const shared_mutex&);
    shared_mutex& operator=(const shared_mutex&);

public:
    XSTD_INLINE shared_mutex() : m_state(0), m_writers_waiting(0) {}

    // ----- exclusive (write) -----
    XSTD_INLINE void lock() {
        m_gate.lock();
        ++m_writers_waiting;
        while (m_state != 0) {
            m_cv.wait(m_gate);
        }
        --m_writers_waiting;
        m_state = -1;
        m_gate.unlock();
    }

    XSTD_INLINE bool try_lock() {
        m_gate.lock();
        if (m_state == 0) {
            m_state = -1;
            m_gate.unlock();
            return true;
        }
        m_gate.unlock();
        return false;
    }

    XSTD_INLINE void unlock() {
        m_gate.lock();
        m_state = 0;
        m_gate.unlock();
        m_cv.notify_all();   // wake any waiting reader or writer
    }

    // ----- shared (read) -----
    XSTD_INLINE void lock_shared() {
        m_gate.lock();
        // Writer-preference: if a writer is queued, new readers wait.
        while (m_state == -1 || m_writers_waiting > 0) {
            m_cv.wait(m_gate);
        }
        ++m_state;
        m_gate.unlock();
    }

    XSTD_INLINE bool try_lock_shared() {
        m_gate.lock();
        if (m_state >= 0 && m_writers_waiting == 0) {
            ++m_state;
            m_gate.unlock();
            return true;
        }
        m_gate.unlock();
        return false;
    }

    XSTD_INLINE void unlock_shared() {
        m_gate.lock();
        --m_state;
        bool last = (m_state == 0);
        m_gate.unlock();
        if (last) {
            m_cv.notify_all();   // last reader out — let a writer in
        }
    }
};


// ---------------------------------------------------------------------------
//  shared_lock<SharedMutex> — RAII for lock_shared / unlock_shared
// ---------------------------------------------------------------------------
template <typename SharedMutex>
class shared_lock {
private:
    SharedMutex& m_lock;

    shared_lock(const shared_lock&);
    shared_lock& operator=(const shared_lock&);

public:
    XSTD_INLINE explicit shared_lock(SharedMutex& m) : m_lock(m) {
        m_lock.lock_shared();
    }
    XSTD_INLINE ~shared_lock() {
        m_lock.unlock_shared();
    }
};

} // namespace xstd

#endif // XSTD_THREADING_SHARED_MUTEX_H
