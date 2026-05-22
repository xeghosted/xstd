// ============================================================================
//  xstd/threading/once.h
//  ---------------------------------------------------------------------------
//  One-shot initialization, modeled after std::call_once / std::once_flag.
//
//  Signature constraint: this dialect has no variadic templates, so
//  call_once takes a plain function pointer void(*)(). If you need to
//  pass state into the init function, capture it in a singleton or pass
//  via a side-channel (e.g. a module-scope pointer).
//
//  State machine over atomic<int>:
//     0 = not yet started
//     1 = initialization in progress
//     2 = initialization completed
//
//  If the init function throws (it shouldn't on Xbox 360 — exceptions are
//  off in most XDK builds), the flag stays in state 1 and subsequent
//  call_once calls spin forever. Same semantics as std::call_once minus
//  the "rotate winner" recovery; that's intentional to keep this small.
// ============================================================================

#ifndef XSTD_THREADING_ONCE_H
#define XSTD_THREADING_ONCE_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#include "atomic.h"

namespace xstd {

struct once_flag {
    atomic<int> state;

    XSTD_INLINE once_flag() : state(0) {}

private:
    once_flag(const once_flag&);
    once_flag& operator=(const once_flag&);
};


XSTD_INLINE void call_once(once_flag& flag, void (*fn)()) {
    // Fast path: already done.
    if (flag.state.load(memory_order_acquire) == 2) {
        return;
    }

    int expected = 0;
    if (flag.state.compare_exchange_strong(expected, 1, memory_order_acq_rel)) {
        // We're the elected initializer.
        fn();
        flag.state.store(2, memory_order_release);
        return;
    }

    // Lost the race: someone else is running fn(). Wait for them.
    while (flag.state.load(memory_order_acquire) != 2) {
        SwitchToThread();
    }
}

} // namespace xstd

#endif // XSTD_THREADING_ONCE_H
