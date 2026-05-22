// ============================================================================
//  xstd/threading/thread.h
//  ---------------------------------------------------------------------------
//  Thin RAII wrapper around CreateThread. Modeled after std::thread but
//  with two deliberate deviations:
//    1. The destructor *auto-joins* a joinable thread instead of calling
//       std::terminate. This is friendlier on Xbox 360 homebrew where
//       there is no clean terminate() path, and matches std::jthread
//       semantics. If you want detach, call detach() explicitly.
//    2. The callable is passed by value, copied to the heap, and invoked
//       on the new thread. The callable must be a copyable type with
//       operator()() (function pointer or functor). No args — capture
//       state in the functor.
//
//  Move semantics use the same detail::move_proxy pattern as unique_ptr,
//  since the dialect has no rvalue references.
// ============================================================================

#ifndef XSTD_THREADING_THREAD_H
#define XSTD_THREADING_THREAD_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {

namespace detail {

template <typename F>
static DWORD WINAPI thread_trampoline(LPVOID arg) {
    F* fn = (F*)arg;
    (*fn)();
    delete fn;
    return 0;
}

struct thread_move_proxy {
    HANDLE h;
    XSTD_INLINE explicit thread_move_proxy(HANDLE x) : h(x) {}
};

} // namespace detail


class thread {
private:
    HANDLE m_handle;

    thread(const thread&);
    thread& operator=(const thread&);

public:
    XSTD_INLINE thread() : m_handle(XSTD_NULL) {}

    template <typename F>
    XSTD_INLINE explicit thread(F fn) {
        F* heap = new F(fn);
        m_handle = CreateThread(NULL, 0, &detail::thread_trampoline<F>,
                                heap, 0, NULL);
        if (!m_handle) {
            delete heap;        // launch failed
        }
    }

    XSTD_INLINE thread(detail::thread_move_proxy p) : m_handle(p.h) {}

    XSTD_INLINE thread& operator=(detail::thread_move_proxy p) {
        if (joinable()) {
            WaitForSingleObject(m_handle, INFINITE);
            CloseHandle(m_handle);
        }
        m_handle = p.h;
        return *this;
    }

    XSTD_INLINE ~thread() {
        // Auto-join (jthread-style). See file header for rationale.
        if (m_handle) {
            WaitForSingleObject(m_handle, INFINITE);
            CloseHandle(m_handle);
        }
    }

    XSTD_INLINE bool joinable() const { return m_handle != XSTD_NULL; }

    XSTD_INLINE void join() {
        if (m_handle) {
            WaitForSingleObject(m_handle, INFINITE);
            CloseHandle(m_handle);
            m_handle = XSTD_NULL;
        }
    }

    XSTD_INLINE void detach() {
        if (m_handle) {
            CloseHandle(m_handle);
            m_handle = XSTD_NULL;
        }
    }

    XSTD_INLINE HANDLE native_handle() const { return m_handle; }
};


XSTD_INLINE detail::thread_move_proxy move(thread& t) {
    HANDLE h = t.native_handle();
    // steal the handle without joining
    // (we know thread has no other ownership state)
    *((HANDLE*)&t) = XSTD_NULL;
    return detail::thread_move_proxy(h);
}


// Free functions modeled on std::this_thread
namespace this_thread {

XSTD_INLINE void yield() {
    SwitchToThread();
}

XSTD_INLINE void sleep_for(unsigned int milliseconds) {
    Sleep((DWORD)milliseconds);
}

} // namespace this_thread

} // namespace xstd

#endif // XSTD_THREADING_THREAD_H
