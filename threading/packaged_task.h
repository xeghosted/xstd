// ============================================================================
//  xstd/threading/packaged_task.h
//  ---------------------------------------------------------------------------
//  Bundles a callable with a future. When you invoke the task via operator(),
//  it runs the callable and writes the return value into the future.
//
//  Differs from C++11 std::packaged_task<R(Args...)>:
//    - signature is just packaged_task<R> (no args; capture state in the
//      callable, same as async)
//    - the stored callable is type-erased via a small virtual base, since
//      we have no std::function
// ============================================================================

#ifndef XSTD_THREADING_PACKAGED_TASK_H
#define XSTD_THREADING_PACKAGED_TASK_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include "future.h"

namespace xstd {

namespace detail {

template <typename R>
struct task_callable_base {
    virtual ~task_callable_base() {}
    virtual R invoke() = 0;
};

template <typename R, typename F>
struct task_callable_impl : task_callable_base<R> {
    F fn;
    XSTD_INLINE task_callable_impl(F f) : fn(f) {}
    virtual R invoke() { return fn(); }
};

} // namespace detail


template <typename R>
class packaged_task {
private:
    detail::task_callable_base<R>* m_fn;
    promise<R>                     m_prom;
    bool                           m_invoked;

    packaged_task(const packaged_task&);
    packaged_task& operator=(const packaged_task&);

public:
    template <typename F>
    XSTD_INLINE explicit packaged_task(F fn)
        : m_fn(new detail::task_callable_impl<R, F>(fn)), m_invoked(false) {}

    XSTD_INLINE ~packaged_task() {
        delete m_fn;
    }

    XSTD_INLINE detail::future_move_proxy<R> get_future() {
        return m_prom.get_future();
    }

    XSTD_INLINE void operator()() {
        if (!m_invoked && m_fn) {
            m_invoked = true;
            R r = m_fn->invoke();
            m_prom.set_value(r);
        }
    }

    XSTD_INLINE bool valid() const { return m_fn != XSTD_NULL && !m_invoked; }
};

} // namespace xstd

#endif // XSTD_THREADING_PACKAGED_TASK_H
