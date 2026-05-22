// ============================================================================
//  xstd/threading/stop_token.h
//  ---------------------------------------------------------------------------
//  Cooperative cancellation primitive, modeled after C++20 stop_source /
//  stop_token. Threads holding a stop_token periodically check
//  stop_requested() and unwind cleanly when set.
//
//  Tokens are cheap copies of a refcounted shared flag. The flag is one-way:
//  once requested, it never goes back to "not requested".
//
//  NOT included: stop_callback. That would need a callback list under a
//  mutex with care around reentrancy; left for a future round if needed.
// ============================================================================

#ifndef XSTD_THREADING_STOP_TOKEN_H
#define XSTD_THREADING_STOP_TOKEN_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include "atomic.h"

namespace xstd {

namespace detail {

struct stop_state {
    atomic<int> refcount;
    atomic<int> stopped;

    XSTD_INLINE stop_state() : refcount(1), stopped(0) {}

    XSTD_INLINE void add_ref() {
        refcount.fetch_add(1, memory_order_acq_rel);
    }
    XSTD_INLINE void release() {
        if (refcount.fetch_sub(1, memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

} // namespace detail


class stop_token {
private:
    detail::stop_state* m_state;

public:
    XSTD_INLINE stop_token() : m_state(XSTD_NULL) {}

    XSTD_INLINE stop_token(detail::stop_state* s) : m_state(s) {
        if (m_state) m_state->add_ref();
    }

    XSTD_INLINE stop_token(const stop_token& o) : m_state(o.m_state) {
        if (m_state) m_state->add_ref();
    }

    XSTD_INLINE stop_token& operator=(const stop_token& o) {
        if (this != &o) {
            if (m_state) m_state->release();
            m_state = o.m_state;
            if (m_state) m_state->add_ref();
        }
        return *this;
    }

    XSTD_INLINE ~stop_token() {
        if (m_state) m_state->release();
    }

    XSTD_INLINE bool stop_requested() const {
        return m_state && m_state->stopped.load(memory_order_acquire) != 0;
    }

    XSTD_INLINE bool stop_possible() const {
        return m_state != XSTD_NULL;
    }
};


class stop_source {
private:
    detail::stop_state* m_state;

    stop_source(const stop_source&);
    stop_source& operator=(const stop_source&);

public:
    XSTD_INLINE stop_source() {
        m_state = new detail::stop_state();
    }

    XSTD_INLINE ~stop_source() {
        if (m_state) m_state->release();
    }

    XSTD_INLINE stop_token get_token() const {
        return stop_token(m_state);
    }

    XSTD_INLINE bool request_stop() {
        int expected = 0;
        return m_state->stopped.compare_exchange_strong(
            expected, 1, memory_order_acq_rel);
    }

    XSTD_INLINE bool stop_requested() const {
        return m_state->stopped.load(memory_order_acquire) != 0;
    }
};

} // namespace xstd

#endif // XSTD_THREADING_STOP_TOKEN_H
