// ============================================================================
//  xstd/threading/future.h
//  ---------------------------------------------------------------------------
//  Single-producer, single-consumer future / promise pair.
//
//  Modeled after std::future / std::promise (C++11), trimmed to what this
//  dialect can express:
//    - no exceptions  -> errors surfaced as future_status::error + an int
//                        error_code the user can set via set_error()
//    - no move ctors  -> ownership transfer via detail::future_move_proxy
//                        (same trick as xstd::unique_ptr)
//    - T must be default-constructible and copy/assign-able
//
//  Lifetime: promise + future share a heap-allocated shared_state behind
//  a refcount (1 for the promise, 1 for the future). Either side may
//  outlive the other. If the promise dies without producing a value,
//  the shared_state is marked broken and waiting futures wake up with
//  future_status_broken.
//
//  Threading: set_value / set_error happen-before any get() / wait() that
//  returns success (release on the producer, acquire on the consumer).
// ============================================================================

#ifndef XSTD_THREADING_FUTURE_H
#define XSTD_THREADING_FUTURE_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include "mutex.h"
#include "condition_variable.h"
#include "atomic.h"

namespace xstd {

enum future_status {
    future_status_pending   = 0,
    future_status_ready     = 1,
    future_status_broken    = 2,
    future_status_error     = 3,
    future_status_timeout   = 4
};


namespace detail {

template <typename T>
struct shared_state {
    atomic<int>         refcount;
    atomic<int>         status;     // future_status_*
    int                 error_code;
    T                   value;
    mutex               m;
    condition_variable  cv;

    // Start with 1 ref (the promise side). promise::get_future() bumps
    // to 2 when the future handle is handed out.
    XSTD_INLINE shared_state()
        : refcount(1), status(future_status_pending), error_code(0), value() {}

    XSTD_INLINE void add_ref() {
        refcount.fetch_add(1, memory_order_acq_rel);
    }
    XSTD_INLINE void release() {
        if (refcount.fetch_sub(1, memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

template <typename T>
struct future_move_proxy {
    shared_state<T>* state;
    XSTD_INLINE explicit future_move_proxy(shared_state<T>* s) : state(s) {}
};

} // namespace detail


// ---------------------------------------------------------------------------
//  future<T>
// ---------------------------------------------------------------------------
template <typename T>
class future {
private:
    detail::shared_state<T>* m_state;

    future(const future&);
    future& operator=(const future&);

public:
    XSTD_INLINE future() : m_state(XSTD_NULL) {}

    XSTD_INLINE future(detail::future_move_proxy<T> p) : m_state(p.state) {}

    XSTD_INLINE future& operator=(detail::future_move_proxy<T> p) {
        if (m_state) m_state->release();
        m_state = p.state;
        return *this;
    }

    XSTD_INLINE ~future() {
        if (m_state) m_state->release();
    }

    XSTD_INLINE bool valid() const { return m_state != XSTD_NULL; }

    XSTD_INLINE future_status wait() {
        if (!m_state) return future_status_broken;
        m_state->m.lock();
        while (m_state->status.load(memory_order_acquire) == future_status_pending) {
            m_state->cv.wait(m_state->m);
        }
        future_status s = (future_status)m_state->status.load(memory_order_acquire);
        m_state->m.unlock();
        return s;
    }

    // Returns one of: future_status_ready, future_status_broken,
    // future_status_error, future_status_timeout.
    XSTD_INLINE future_status wait_for(unsigned int milliseconds) {
        if (!m_state) return future_status_broken;
        m_state->m.lock();
        while (m_state->status.load(memory_order_acquire) == future_status_pending) {
            if (m_state->cv.wait_for(m_state->m, milliseconds) == cv_status_timeout) {
                future_status s = (future_status)m_state->status.load(memory_order_acquire);
                m_state->m.unlock();
                return (s == future_status_pending) ? future_status_timeout : s;
            }
        }
        future_status s = (future_status)m_state->status.load(memory_order_acquire);
        m_state->m.unlock();
        return s;
    }

    // Blocks until ready/broken/error. Returns the value if ready; if the
    // result was broken or an error, the value will be a default-constructed
    // T and the caller should check status() first via wait().
    XSTD_INLINE T get() {
        wait();
        T v = m_state->value;
        return v;
    }

    XSTD_INLINE int error_code() const {
        return m_state ? m_state->error_code : 0;
    }

    // Release the underlying shared_state for transfer to a shared_future.
    // After calling this, valid() returns false. Use future::share() below.
    XSTD_INLINE detail::shared_state<T>* _release_state() {
        detail::shared_state<T>* s = m_state;
        m_state = XSTD_NULL;
        return s;
    }
};


// ---------------------------------------------------------------------------
//  shared_future<T>
// ---------------------------------------------------------------------------
//  Copyable view of a future result. Multiple shared_future instances can
//  observe the same value; each get() returns a copy of T. Construct via
//  future<T>::share().
// ---------------------------------------------------------------------------
template <typename T>
class shared_future {
private:
    detail::shared_state<T>* m_state;

public:
    XSTD_INLINE shared_future() : m_state(XSTD_NULL) {}

    XSTD_INLINE shared_future(detail::future_move_proxy<T> p) : m_state(p.state) {}

    XSTD_INLINE shared_future(const shared_future& o) : m_state(o.m_state) {
        if (m_state) m_state->add_ref();
    }

    XSTD_INLINE shared_future& operator=(const shared_future& o) {
        if (this != &o) {
            if (m_state) m_state->release();
            m_state = o.m_state;
            if (m_state) m_state->add_ref();
        }
        return *this;
    }

    XSTD_INLINE shared_future& operator=(detail::future_move_proxy<T> p) {
        if (m_state) m_state->release();
        m_state = p.state;
        return *this;
    }

    XSTD_INLINE ~shared_future() {
        if (m_state) m_state->release();
    }

    XSTD_INLINE bool valid() const { return m_state != XSTD_NULL; }

    XSTD_INLINE future_status wait() {
        if (!m_state) return future_status_broken;
        m_state->m.lock();
        while (m_state->status.load(memory_order_acquire) == future_status_pending) {
            m_state->cv.wait(m_state->m);
        }
        future_status s = (future_status)m_state->status.load(memory_order_acquire);
        m_state->m.unlock();
        return s;
    }

    XSTD_INLINE T get() {
        wait();
        return m_state->value;
    }

    XSTD_INLINE int error_code() const {
        return m_state ? m_state->error_code : 0;
    }
};


// Free helper: convert future<T> into shared_future<T>. Original future
// becomes invalid afterwards (matches std::future::share semantics).
template <typename T>
XSTD_INLINE shared_future<T> share(future<T>& f) {
    return shared_future<T>(detail::future_move_proxy<T>(f._release_state()));
}


// ---------------------------------------------------------------------------
//  promise<T>
// ---------------------------------------------------------------------------
template <typename T>
class promise {
private:
    detail::shared_state<T>* m_state;
    int                      m_future_taken;

    promise(const promise&);
    promise& operator=(const promise&);

public:
    XSTD_INLINE promise() : m_future_taken(0) {
        m_state = new detail::shared_state<T>();
    }

    XSTD_INLINE ~promise() {
        if (m_state) {
            // If we go away without producing a value, the consumer side
            // sees a broken promise.
            int expected = future_status_pending;
            if (m_state->status.compare_exchange_strong(
                    expected, future_status_broken, memory_order_acq_rel)) {
                m_state->m.lock();
                m_state->m.unlock();
                m_state->cv.notify_all();
            }
            m_state->release();
        }
    }

    // Hands the consumer end out. Should only be called once.
    XSTD_INLINE detail::future_move_proxy<T> get_future() {
        m_state->add_ref();              // ref count for the future handle
        m_future_taken = 1;
        return detail::future_move_proxy<T>(m_state);
    }

    XSTD_INLINE void set_value(const T& v) {
        m_state->m.lock();
        m_state->value = v;
        m_state->status.store(future_status_ready, memory_order_release);
        m_state->m.unlock();
        m_state->cv.notify_all();
    }

    XSTD_INLINE void set_error(int code) {
        m_state->m.lock();
        m_state->error_code = code;
        m_state->status.store(future_status_error, memory_order_release);
        m_state->m.unlock();
        m_state->cv.notify_all();
    }
};

} // namespace xstd

#endif // XSTD_THREADING_FUTURE_H
