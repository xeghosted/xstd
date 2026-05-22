// ============================================================================
//  xstd/threading/atomic.h
//  ---------------------------------------------------------------------------
//  Lock-free atomic primitives for Xbox 360 (Xenon, PowerPC, big-endian).
//
//  Modeled after C++11 std::atomic<T>, implemented in the dialect the XDK
//  compiler accepts (no rvalue refs, no variadic templates, no <type_traits>,
//  no <atomic>, no static_assert, no constexpr).
//
//  Provides:
//    - xstd::memory_order               relaxed/acquire/release/acq_rel/seq_cst
//    - xstd::atomic<T>                  for int32, int64, bool, and pointer T*
//    - xstd::atomic_thread_fence(order) free function
//    - xstd::atomic_flag                test_and_set / clear
//
//  Implementation notes:
//    - RMW ops use the Interlocked* family from the Xbox 360 XDK. These are
//      already lock-free (lwarx/stwcx internally) and include a full barrier,
//      so memory_order is effectively upgraded to seq_cst for RMW. That is
//      over-synchronized but correct — never under.
//    - Plain load/store get explicit barriers around them via the intrinsics
//      below.
//    - Barriers:    __lwsync (acquire/release/acq_rel)
//                   __sync   (seq_cst, full hwsync)
//                   __isync  (post-acquire, kills speculation past load)
//    - atomic<T*> and atomic<bool> reuse the 32-bit integer codepath.
//      Xbox 360 user-mode pointers are 32-bit, so this is safe.
//    - Big-endian: integer atomics work on whole machine words, no byte
//      swap involved. Sub-word atomics are NOT provided (would need masking
//      against the containing word; not in the spec).
// ============================================================================

#ifndef XSTD_THREADING_ATOMIC_H
#define XSTD_THREADING_ATOMIC_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

// PowerPC barrier intrinsics (__lwsync / __sync / __isync).
#include <ppcintrinsics.h>

// Interlocked* RMW primitives. The translation unit normally already pulls
// in xtl.h, but include it here defensively so this header is standalone.
#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {

// ---------------------------------------------------------------------------
//  memory_order
// ---------------------------------------------------------------------------
//  Values match the conventional C++11 enumeration order so they can be
//  compared with simple <= / >= if ever needed downstream.
enum memory_order {
    memory_order_relaxed = 0,
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
};


namespace detail {

// Compile-time guard substitute (no static_assert in this dialect): an
// instantiation with the wrong size produces a negative-sized array typedef,
// which the compiler reports cleanly.
template <int N> struct atomic_size_check { typedef char type[N > 0 ? 1 : -1]; };


// ---------------------------------------------------------------------------
//  Barrier helpers
// ---------------------------------------------------------------------------
//  The PowerPC mapping used here is the conventional GCC/IBM one:
//
//    load relaxed   : plain load
//    load acquire   : load; isync          (kills downstream speculation)
//    load seq_cst   : sync; load; isync
//
//    store relaxed  : plain store
//    store release  : lwsync; store
//    store seq_cst  : sync; store; sync
//
//    RMW relaxed    : ll/sc loop
//    RMW acquire    : ll/sc loop; isync
//    RMW release    : lwsync; ll/sc loop
//    RMW acq_rel    : lwsync; ll/sc loop; isync
//    RMW seq_cst    : sync;   ll/sc loop; isync
//
//  isync after the load works because the conditional branch on the loaded
//  value creates the dependency the architecture requires for an acquire.
// ---------------------------------------------------------------------------

XSTD_INLINE void barrier_pre_load(memory_order o) {
    if (o == memory_order_seq_cst) {
        __sync();
    }
}

XSTD_INLINE void barrier_post_load(memory_order o) {
    if (o == memory_order_acquire || o == memory_order_acq_rel || o == memory_order_seq_cst) {
        __isync();
    }
}

XSTD_INLINE void barrier_pre_store(memory_order o) {
    if (o == memory_order_release || o == memory_order_acq_rel) {
        __lwsync();
    } else if (o == memory_order_seq_cst) {
        __sync();
    }
}

XSTD_INLINE void barrier_post_store(memory_order o) {
    if (o == memory_order_seq_cst) {
        __sync();
    }
}

XSTD_INLINE void barrier_pre_rmw(memory_order o) {
    if (o == memory_order_release || o == memory_order_acq_rel) {
        __lwsync();
    } else if (o == memory_order_seq_cst) {
        __sync();
    }
}

XSTD_INLINE void barrier_post_rmw(memory_order o) {
    if (o == memory_order_acquire || o == memory_order_acq_rel || o == memory_order_seq_cst) {
        __isync();
    }
}


// ---------------------------------------------------------------------------
//  32-bit primitive layer
// ---------------------------------------------------------------------------
//  Backed by the XDK Interlocked* APIs. Each RMW already implies a full
//  barrier on Xenon, so the memory_order parameter is honored *upward* only
//  (relaxed/acquire/release/acq_rel all behave as seq_cst). For pure load
//  and store we add explicit __lwsync / __sync / __isync.
// ---------------------------------------------------------------------------
XSTD_INLINE int load32(volatile int* p, memory_order o) {
    barrier_pre_load(o);
    int v = *p;
    barrier_post_load(o);
    return v;
}

XSTD_INLINE void store32(volatile int* p, int v, memory_order o) {
    barrier_pre_store(o);
    *p = v;
    barrier_post_store(o);
}

XSTD_INLINE int exchange32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedExchange((volatile LONG*)p, (LONG)v);
}

XSTD_INLINE int cas32_strong(volatile int* p, int* expected, int desired, memory_order /*o*/) {
    LONG prev = InterlockedCompareExchange((volatile LONG*)p,
                                           (LONG)desired,
                                           (LONG)(*expected));
    if (prev == (LONG)(*expected)) {
        return 1;
    }
    *expected = (int)prev;
    return 0;
}

// No "weak" CAS on the Xbox 360 — Interlocked* does not spuriously fail,
// so weak and strong have identical semantics here.
XSTD_INLINE int cas32_weak(volatile int* p, int* expected, int desired, memory_order o) {
    return cas32_strong(p, expected, desired, o);
}

XSTD_INLINE int fetch_add32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedExchangeAdd((volatile LONG*)p, (LONG)v);
}

XSTD_INLINE int fetch_sub32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedExchangeAdd((volatile LONG*)p, -(LONG)v);
}

XSTD_INLINE int fetch_and32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedAnd((volatile LONG*)p, (LONG)v);
}

XSTD_INLINE int fetch_or32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedOr((volatile LONG*)p, (LONG)v);
}

XSTD_INLINE int fetch_xor32(volatile int* p, int v, memory_order /*o*/) {
    return (int)InterlockedXor((volatile LONG*)p, (LONG)v);
}


// ---------------------------------------------------------------------------
//  64-bit primitive layer
// ---------------------------------------------------------------------------
XSTD_INLINE __int64 load64(volatile __int64* p, memory_order o) {
    barrier_pre_load(o);
    // A naturally-aligned 64-bit load is single-instruction (ld) on Xenon
    // and is therefore atomic by itself.
    __int64 v = *p;
    barrier_post_load(o);
    return v;
}

XSTD_INLINE void store64(volatile __int64* p, __int64 v, memory_order o) {
    barrier_pre_store(o);
    *p = v;
    barrier_post_store(o);
}

XSTD_INLINE __int64 exchange64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v);
}

XSTD_INLINE int cas64_strong(volatile __int64* p, __int64* expected, __int64 desired, memory_order /*o*/) {
    LONGLONG prev = InterlockedCompareExchange64((volatile LONGLONG*)p,
                                                 (LONGLONG)desired,
                                                 (LONGLONG)(*expected));
    if (prev == (LONGLONG)(*expected)) {
        return 1;
    }
    *expected = (__int64)prev;
    return 0;
}

XSTD_INLINE int cas64_weak(volatile __int64* p, __int64* expected, __int64 desired, memory_order o) {
    return cas64_strong(p, expected, desired, o);
}

XSTD_INLINE __int64 fetch_add64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedExchangeAdd64((volatile LONGLONG*)p, (LONGLONG)v);
}

XSTD_INLINE __int64 fetch_sub64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedExchangeAdd64((volatile LONGLONG*)p, -(LONGLONG)v);
}

XSTD_INLINE __int64 fetch_and64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedAnd64((volatile LONGLONG*)p, (LONGLONG)v);
}

XSTD_INLINE __int64 fetch_or64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedOr64((volatile LONGLONG*)p, (LONGLONG)v);
}

XSTD_INLINE __int64 fetch_xor64(volatile __int64* p, __int64 v, memory_order /*o*/) {
    return InterlockedXor64((volatile LONGLONG*)p, (LONGLONG)v);
}


// ---------------------------------------------------------------------------
//  Tag dispatch by sizeof(T). The primary template is intentionally
//  undefined — instantiating atomic<T> with an unsupported size errors out
//  at link time on the missing specialization (cleaner than SFINAE here).
// ---------------------------------------------------------------------------
template <typename T, int Size> struct atomic_storage;

// 4-byte specialization (int, unsigned, bool-as-int, pointer)
template <typename T> struct atomic_storage<T, 4> {
    volatile int v;

    XSTD_INLINE atomic_storage()       { v = 0; }
    XSTD_INLINE atomic_storage(T init) { v = (int)init; }

    XSTD_INLINE T load(memory_order o) const {
        return (T)load32((volatile int*)&v, o);
    }
    XSTD_INLINE void store(T x, memory_order o) {
        store32(&v, (int)x, o);
    }
    XSTD_INLINE T exchange(T x, memory_order o) {
        return (T)exchange32(&v, (int)x, o);
    }
    XSTD_INLINE bool compare_exchange_weak(T& expected, T desired, memory_order o) {
        int e = (int)expected;
        int r = cas32_weak(&v, &e, (int)desired, o);
        if (!r) expected = (T)e;
        return r != 0;
    }
    XSTD_INLINE bool compare_exchange_strong(T& expected, T desired, memory_order o) {
        int e = (int)expected;
        int r = cas32_strong(&v, &e, (int)desired, o);
        if (!r) expected = (T)e;
        return r != 0;
    }
    XSTD_INLINE T fetch_add(T x, memory_order o) { return (T)fetch_add32(&v, (int)x, o); }
    XSTD_INLINE T fetch_sub(T x, memory_order o) { return (T)fetch_sub32(&v, (int)x, o); }
    XSTD_INLINE T fetch_and(T x, memory_order o) { return (T)fetch_and32(&v, (int)x, o); }
    XSTD_INLINE T fetch_or (T x, memory_order o) { return (T)fetch_or32 (&v, (int)x, o); }
    XSTD_INLINE T fetch_xor(T x, memory_order o) { return (T)fetch_xor32(&v, (int)x, o); }
};

// 8-byte specialization (long long, unsigned long long)
template <typename T> struct atomic_storage<T, 8> {
    volatile __int64 v;

    XSTD_INLINE atomic_storage()       { v = 0; }
    XSTD_INLINE atomic_storage(T init) { v = (__int64)init; }

    XSTD_INLINE T load(memory_order o) const {
        return (T)load64((volatile __int64*)&v, o);
    }
    XSTD_INLINE void store(T x, memory_order o) {
        store64(&v, (__int64)x, o);
    }
    XSTD_INLINE T exchange(T x, memory_order o) {
        return (T)exchange64(&v, (__int64)x, o);
    }
    XSTD_INLINE bool compare_exchange_weak(T& expected, T desired, memory_order o) {
        __int64 e = (__int64)expected;
        int r = cas64_weak(&v, &e, (__int64)desired, o);
        if (!r) expected = (T)e;
        return r != 0;
    }
    XSTD_INLINE bool compare_exchange_strong(T& expected, T desired, memory_order o) {
        __int64 e = (__int64)expected;
        int r = cas64_strong(&v, &e, (__int64)desired, o);
        if (!r) expected = (T)e;
        return r != 0;
    }
    XSTD_INLINE T fetch_add(T x, memory_order o) { return (T)fetch_add64(&v, (__int64)x, o); }
    XSTD_INLINE T fetch_sub(T x, memory_order o) { return (T)fetch_sub64(&v, (__int64)x, o); }
    XSTD_INLINE T fetch_and(T x, memory_order o) { return (T)fetch_and64(&v, (__int64)x, o); }
    XSTD_INLINE T fetch_or (T x, memory_order o) { return (T)fetch_or64 (&v, (__int64)x, o); }
    XSTD_INLINE T fetch_xor(T x, memory_order o) { return (T)fetch_xor64(&v, (__int64)x, o); }
};

} // namespace detail


// ---------------------------------------------------------------------------
//  atomic<T> — primary template for integer-like types
// ---------------------------------------------------------------------------
//  Default-constructs to zero (matches std::atomic value-init semantics for
//  integer T). Non-copyable.
template <typename T>
class atomic {
private:
    detail::atomic_storage<T, sizeof(T)> m;

    atomic(const atomic&);
    atomic& operator=(const atomic&);

public:
    typedef T value_type;

    XSTD_INLINE atomic() : m() {}
    XSTD_INLINE atomic(T init) : m(init) {}

    XSTD_INLINE T    load(memory_order o = memory_order_seq_cst) const { return m.load(o); }
    XSTD_INLINE void store(T v, memory_order o = memory_order_seq_cst) { m.store(v, o); }
    XSTD_INLINE T    exchange(T v, memory_order o = memory_order_seq_cst) { return m.exchange(v, o); }

    XSTD_INLINE bool compare_exchange_weak(T& expected, T desired,
                                           memory_order o = memory_order_seq_cst) {
        return m.compare_exchange_weak(expected, desired, o);
    }
    XSTD_INLINE bool compare_exchange_strong(T& expected, T desired,
                                             memory_order o = memory_order_seq_cst) {
        return m.compare_exchange_strong(expected, desired, o);
    }

    XSTD_INLINE T fetch_add(T v, memory_order o = memory_order_seq_cst) { return m.fetch_add(v, o); }
    XSTD_INLINE T fetch_sub(T v, memory_order o = memory_order_seq_cst) { return m.fetch_sub(v, o); }
    XSTD_INLINE T fetch_and(T v, memory_order o = memory_order_seq_cst) { return m.fetch_and(v, o); }
    XSTD_INLINE T fetch_or (T v, memory_order o = memory_order_seq_cst) { return m.fetch_or (v, o); }
    XSTD_INLINE T fetch_xor(T v, memory_order o = memory_order_seq_cst) { return m.fetch_xor(v, o); }

    XSTD_INLINE T operator++()    { return fetch_add((T)1, memory_order_seq_cst) + (T)1; }
    XSTD_INLINE T operator++(int) { return fetch_add((T)1, memory_order_seq_cst); }
    XSTD_INLINE T operator--()    { return fetch_sub((T)1, memory_order_seq_cst) - (T)1; }
    XSTD_INLINE T operator--(int) { return fetch_sub((T)1, memory_order_seq_cst); }
    XSTD_INLINE T operator+=(T v) { return fetch_add(v, memory_order_seq_cst) + v; }
    XSTD_INLINE T operator-=(T v) { return fetch_sub(v, memory_order_seq_cst) - v; }
    XSTD_INLINE T operator&=(T v) { return fetch_and(v, memory_order_seq_cst) & v; }
    XSTD_INLINE T operator|=(T v) { return fetch_or (v, memory_order_seq_cst) | v; }
    XSTD_INLINE T operator^=(T v) { return fetch_xor(v, memory_order_seq_cst) ^ v; }

    XSTD_INLINE operator T() const { return load(memory_order_seq_cst); }
    XSTD_INLINE T operator=(T v)   { store(v, memory_order_seq_cst); return v; }
};


// ---------------------------------------------------------------------------
//  atomic<bool> — specialization (no arithmetic ops, no bitwise ops)
// ---------------------------------------------------------------------------
template <>
class atomic<bool> {
private:
    detail::atomic_storage<int, 4> m;

    atomic(const atomic&);
    atomic& operator=(const atomic&);

public:
    typedef bool value_type;

    XSTD_INLINE atomic() : m() {}
    XSTD_INLINE atomic(bool init) : m(init ? 1 : 0) {}

    XSTD_INLINE bool load(memory_order o = memory_order_seq_cst) const {
        return m.load(o) != 0;
    }
    XSTD_INLINE void store(bool v, memory_order o = memory_order_seq_cst) {
        m.store(v ? 1 : 0, o);
    }
    XSTD_INLINE bool exchange(bool v, memory_order o = memory_order_seq_cst) {
        return m.exchange(v ? 1 : 0, o) != 0;
    }
    XSTD_INLINE bool compare_exchange_weak(bool& expected, bool desired,
                                           memory_order o = memory_order_seq_cst) {
        int e = expected ? 1 : 0;
        bool r = m.compare_exchange_weak(e, desired ? 1 : 0, o);
        if (!r) expected = (e != 0);
        return r;
    }
    XSTD_INLINE bool compare_exchange_strong(bool& expected, bool desired,
                                             memory_order o = memory_order_seq_cst) {
        int e = expected ? 1 : 0;
        bool r = m.compare_exchange_strong(e, desired ? 1 : 0, o);
        if (!r) expected = (e != 0);
        return r;
    }

    XSTD_INLINE operator bool() const { return load(memory_order_seq_cst); }
    XSTD_INLINE bool operator=(bool v) { store(v, memory_order_seq_cst); return v; }
};


// ---------------------------------------------------------------------------
//  atomic<T*> — pointer specialization
// ---------------------------------------------------------------------------
//  Xbox 360 user-mode is 32-bit, so pointers fit into the 4-byte path.
//  fetch_add / fetch_sub take a ptrdiff in *elements*, matching std::atomic.
template <typename T>
class atomic<T*> {
private:
    detail::atomic_storage<T*, sizeof(T*)> m;

    atomic(const atomic&);
    atomic& operator=(const atomic&);

public:
    typedef T* value_type;

    XSTD_INLINE atomic() : m() {}
    XSTD_INLINE atomic(T* init) : m(init) {}

    XSTD_INLINE T* load(memory_order o = memory_order_seq_cst) const { return m.load(o); }
    XSTD_INLINE void store(T* v, memory_order o = memory_order_seq_cst) { m.store(v, o); }
    XSTD_INLINE T* exchange(T* v, memory_order o = memory_order_seq_cst) { return m.exchange(v, o); }

    XSTD_INLINE bool compare_exchange_weak(T*& expected, T* desired,
                                           memory_order o = memory_order_seq_cst) {
        return m.compare_exchange_weak(expected, desired, o);
    }
    XSTD_INLINE bool compare_exchange_strong(T*& expected, T* desired,
                                             memory_order o = memory_order_seq_cst) {
        return m.compare_exchange_strong(expected, desired, o);
    }

    XSTD_INLINE T* fetch_add(int n, memory_order o = memory_order_seq_cst) {
        // element-wise pointer arithmetic, like std::atomic<T*>
        return (T*)detail::fetch_add32((volatile int*)&m.v, (int)(n * (int)sizeof(T)), o);
    }
    XSTD_INLINE T* fetch_sub(int n, memory_order o = memory_order_seq_cst) {
        return (T*)detail::fetch_sub32((volatile int*)&m.v, (int)(n * (int)sizeof(T)), o);
    }

    XSTD_INLINE operator T*() const { return load(memory_order_seq_cst); }
    XSTD_INLINE T* operator=(T* v)  { store(v, memory_order_seq_cst); return v; }
};


// ---------------------------------------------------------------------------
//  atomic_thread_fence
// ---------------------------------------------------------------------------
XSTD_INLINE void atomic_thread_fence(memory_order o) {
    switch (o) {
    case memory_order_relaxed:
        break;
    case memory_order_acquire:
    case memory_order_release:
    case memory_order_acq_rel:
        __lwsync();
        break;
    case memory_order_seq_cst:
    default:
        __sync();
        break;
    }
}


// ---------------------------------------------------------------------------
//  atomic_flag
// ---------------------------------------------------------------------------
//  A guaranteed-lock-free boolean flag. Backed by a 32-bit word so the same
//  ll/sc path is used.
class atomic_flag {
private:
    volatile int m_v;

    atomic_flag(const atomic_flag&);
    atomic_flag& operator=(const atomic_flag&);

public:
    XSTD_INLINE atomic_flag() { m_v = 0; }

    XSTD_INLINE bool test_and_set(memory_order o = memory_order_seq_cst) {
        return detail::exchange32(&m_v, 1, o) != 0;
    }

    XSTD_INLINE void clear(memory_order o = memory_order_seq_cst) {
        detail::store32(&m_v, 0, o);
    }
};

} // namespace xstd

#endif // XSTD_THREADING_ATOMIC_H
