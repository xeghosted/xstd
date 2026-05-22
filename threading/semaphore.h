// ============================================================================
//  xstd/threading/semaphore.h
//  ---------------------------------------------------------------------------
//  Counting semaphore. Thin wrapper around the Xbox 360 kernel semaphore
//  (CreateSemaphore / ReleaseSemaphore / WaitForSingleObject).
//
//  API roughly tracks std::counting_semaphore:
//    - acquire()                    block until count > 0, then count--
//    - try_acquire()                non-blocking attempt
//    - try_acquire_for(ms)          timed attempt (milliseconds)
//    - release(n = 1)               count += n, may wake n waiters
//
//  Max count is fixed at construction; default 0x7FFFFFFF (effectively
//  unbounded for game-side use).
// ============================================================================

#ifndef XSTD_THREADING_SEMAPHORE_H
#define XSTD_THREADING_SEMAPHORE_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {

class semaphore {
private:
    HANDLE m_h;

    semaphore(const semaphore&);
    semaphore& operator=(const semaphore&);

public:
    XSTD_INLINE explicit semaphore(int initial_count = 0, int max_count = 0x7FFFFFFF) {
        m_h = CreateSemaphore(NULL,
                              (LONG)initial_count,
                              (LONG)max_count,
                              NULL);
    }

    XSTD_INLINE ~semaphore() {
        if (m_h) CloseHandle(m_h);
    }

    XSTD_INLINE void acquire() {
        WaitForSingleObject(m_h, INFINITE);
    }

    XSTD_INLINE bool try_acquire() {
        return WaitForSingleObject(m_h, 0) == WAIT_OBJECT_0;
    }

    XSTD_INLINE bool try_acquire_for(unsigned int milliseconds) {
        return WaitForSingleObject(m_h, (DWORD)milliseconds) == WAIT_OBJECT_0;
    }

    XSTD_INLINE void release(int n = 1) {
        ReleaseSemaphore(m_h, (LONG)n, NULL);
    }

    XSTD_INLINE HANDLE native_handle() { return m_h; }
};

} // namespace xstd

#endif // XSTD_THREADING_SEMAPHORE_H
