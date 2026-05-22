// ============================================================================
//  xstd/threading/mutex.h
//  ---------------------------------------------------------------------------
//  Recursive mutex backed by Win32 CRITICAL_SECTION (which is what the
//  Xbox 360 kernel provides). Includes a generic lock_guard<Lockable> RAII
//  helper that also works with xstd::spinlock and any other type exposing
//  lock() / unlock().
//
//  CRITICAL_SECTION on Xbox 360 is:
//    - recursive (the same thread may re-enter)
//    - lock-free in the uncontended case (interlocked CAS only)
//    - falls back to a kernel wait on contention
// ============================================================================

#ifndef XSTD_THREADING_MUTEX_H
#define XSTD_THREADING_MUTEX_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {

// ---------------------------------------------------------------------------
//  mutex — recursive, native
// ---------------------------------------------------------------------------
class mutex {
private:
    CRITICAL_SECTION m_cs;

    mutex(const mutex&);
    mutex& operator=(const mutex&);

public:
    XSTD_INLINE mutex() {
        InitializeCriticalSection(&m_cs);
    }
    XSTD_INLINE ~mutex() {
        DeleteCriticalSection(&m_cs);
    }

    XSTD_INLINE void lock() {
        EnterCriticalSection(&m_cs);
    }
    XSTD_INLINE bool try_lock() {
        return TryEnterCriticalSection(&m_cs) != FALSE;
    }
    XSTD_INLINE void unlock() {
        LeaveCriticalSection(&m_cs);
    }

    // exposed so xstd::condition_variable can hand the raw CS to kernel APIs
    XSTD_INLINE CRITICAL_SECTION* native_handle() {
        return &m_cs;
    }
};


// ---------------------------------------------------------------------------
//  lock_guard<Lockable> — scoped lock RAII
// ---------------------------------------------------------------------------
//  Works with anything that has lock() / unlock(): xstd::mutex,
//  xstd::spinlock, custom types.
// ---------------------------------------------------------------------------
template <typename Lockable>
class lock_guard {
private:
    Lockable& m_lock;

    lock_guard(const lock_guard&);
    lock_guard& operator=(const lock_guard&);

public:
    XSTD_INLINE explicit lock_guard(Lockable& l) : m_lock(l) {
        m_lock.lock();
    }
    XSTD_INLINE ~lock_guard() {
        m_lock.unlock();
    }
};

} // namespace xstd

#endif // XSTD_THREADING_MUTEX_H
