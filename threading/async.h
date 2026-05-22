// ============================================================================
//  xstd/threading/async.h
//  ---------------------------------------------------------------------------
//  Fire-and-forget execution of a callable on a fresh thread, returning a
//  future<R> for the result. Spec is the C++11 std::async with launch::async
//  policy; there is no deferred policy here (would need lazy evaluation).
//
//  Usage:
//      int compute() { return 42; }
//      xstd::future<int> f = xstd::async<int>(compute);
//      int v = f.get();
//
//  R must be specified explicitly because the dialect lacks the type
//  deduction (decltype + result_of) needed to infer it from F.
//
//  F may be any copyable callable taking no arguments and returning R:
//    - function pointer  R (*)()
//    - functor with R operator()()
//    - early-C++11 lambda (if your compiler accepts them)
//  Capture state via the functor, since variadic args aren't available.
//
//  Return value: a future<R>. The spawned thread is detached internally;
//  its lifetime is owned by the future via the shared_state.
// ============================================================================

#ifndef XSTD_THREADING_ASYNC_H
#define XSTD_THREADING_ASYNC_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#include "future.h"

namespace xstd {

namespace detail {

template <typename R, typename F>
struct async_runner {
    F             fn;
    promise<R>*   prom;

    XSTD_INLINE async_runner(F f, promise<R>* p) : fn(f), prom(p) {}
};

template <typename R, typename F>
static DWORD WINAPI async_trampoline(LPVOID arg) {
    async_runner<R, F>* r = (async_runner<R, F>*)arg;
    R result = r->fn();
    r->prom->set_value(result);
    delete r->prom;       // promise lifetime ends here; future still holds shared_state ref
    delete r;
    return 0;
}

} // namespace detail


// Returns the move_proxy directly so it can be returned by value without
// needing a copy/move ctor on future<R> (same idiom as xstd::make_unique).
// The receiver constructs/assigns: future<int> f = async<int>(...);
template <typename R, typename F>
XSTD_INLINE detail::future_move_proxy<R> async(F fn) {
    promise<R>* p = new promise<R>();
    detail::future_move_proxy<R> fut_proxy = p->get_future();

    detail::async_runner<R, F>* runner = new detail::async_runner<R, F>(fn, p);
    HANDLE h = CreateThread(NULL, 0,
                            &detail::async_trampoline<R, F>,
                            runner, 0, NULL);
    if (h) {
        CloseHandle(h);   // detached; future drives shared_state lifetime
    } else {
        // launch failed: promise dtor on the trampoline path never runs,
        // so do it here. proxy now points to a state that will be released
        // by the receiving future's dtor.
        delete p;
        delete runner;
    }
    return fut_proxy;
}

} // namespace xstd

#endif // XSTD_THREADING_ASYNC_H
