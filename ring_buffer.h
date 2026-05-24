// ============================================================================
//  xstd/ring_buffer.h
//  ---------------------------------------------------------------------------
//  Bounded FIFO with N fixed slots, single-threaded use. No allocation, ever.
//  Overflow on push_back / underflow on pop_front trigger XSTD_ASSERT.
//
//  Provides:
//    - xstd::ring_buffer<T, N>
//        ring_buffer()                              empty
//        ring_buffer(const ring_buffer&)            deep copy in fifo order
//        ~ring_buffer()                             destroys live elements
//
//        ring_buffer& operator=(const ring_buffer&)
//
//        unsigned int size()     const
//        unsigned int capacity() const              == N
//        bool         empty() const
//        bool         full()  const
//
//        T&           front();   const T& front() const
//        T&           back();    const T& back()  const
//
//        void         push_back(const T& v)         asserts !full()
//        void         pop_front()                   asserts !empty()
//        void         clear()
//
//        void         emplace_back0()
//        void         emplace_back1(const A&)
//        void         emplace_back2(const A&, const B&)
//        void         emplace_back3(const A&, const B&, const C&)
//
//  Notes:
//    - Single-threaded. Not safe across producer / consumer threads — for
//      that, an SPSC variant should be built on top of xstd::atomic.
//    - Storage is `char buf[sizeof(T)*N]`, aligned to 8 bytes. T must have
//      natural alignment <= 8.
//    - No iterators — element order wraps and exposing T* would mislead.
//      Iterate by repeated front() + pop_front() if needed.
// ============================================================================

#ifndef XSTD_RING_BUFFER_H
#define XSTD_RING_BUFFER_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include "assert.h"

namespace xstd {

template <typename T, unsigned int N>
class ring_buffer {
public:
    XSTD_INLINE ring_buffer() : head_(0), size_(0) {}

    XSTD_INLINE ring_buffer(const ring_buffer& other) : head_(0), size_(0) {
        copy_from(other);
    }

    XSTD_INLINE ~ring_buffer() { clear(); }

    XSTD_INLINE ring_buffer& operator=(const ring_buffer& other) {
        if (this == &other) return *this;
        clear();
        copy_from(other);
        return *this;
    }

    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return N; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }
    XSTD_INLINE bool         full()  const { return size_ == N; }

    XSTD_INLINE T&       front()       { XSTD_ASSERT(size_ > 0); return *ptr(head_); }
    XSTD_INLINE const T& front() const { XSTD_ASSERT(size_ > 0); return *ptr_const(head_); }
    XSTD_INLINE T&       back()        { XSTD_ASSERT(size_ > 0); return *ptr(index(head_ + size_ - 1)); }
    XSTD_INLINE const T& back()  const { XSTD_ASSERT(size_ > 0); return *ptr_const(index(head_ + size_ - 1)); }

    XSTD_INLINE void push_back(const T& v) {
        XSTD_ASSERT(size_ < N);
        unsigned int slot = index(head_ + size_);
        new (raw(slot)) T(v);
        ++size_;
    }

    XSTD_INLINE void pop_front() {
        XSTD_ASSERT(size_ > 0);
        ptr(head_)->~T();
        head_ = index(head_ + 1);
        --size_;
    }

    XSTD_INLINE void clear() {
        while (size_ > 0) {
            ptr(head_)->~T();
            head_ = index(head_ + 1);
            --size_;
        }
        head_ = 0;
    }

    XSTD_INLINE void emplace_back0() {
        XSTD_ASSERT(size_ < N);
        unsigned int slot = index(head_ + size_);
        new (raw(slot)) T();
        ++size_;
    }
    template <typename A>
    XSTD_INLINE void emplace_back1(const A& a) {
        XSTD_ASSERT(size_ < N);
        unsigned int slot = index(head_ + size_);
        new (raw(slot)) T(a);
        ++size_;
    }
    template <typename A, typename B>
    XSTD_INLINE void emplace_back2(const A& a, const B& b) {
        XSTD_ASSERT(size_ < N);
        unsigned int slot = index(head_ + size_);
        new (raw(slot)) T(a, b);
        ++size_;
    }
    template <typename A, typename B, typename C>
    XSTD_INLINE void emplace_back3(const A& a, const B& b, const C& c) {
        XSTD_ASSERT(size_ < N);
        unsigned int slot = index(head_ + size_);
        new (raw(slot)) T(a, b, c);
        ++size_;
    }

private:
    XSTD_INLINE static unsigned int index(unsigned int i) { return i % N; }
    XSTD_INLINE void*    raw(unsigned int i)       { return static_cast<void*>(storage_.buf + i * sizeof(T)); }
    XSTD_INLINE T*       ptr(unsigned int i)       { return reinterpret_cast<T*>(storage_.buf + i * sizeof(T)); }
    XSTD_INLINE const T* ptr_const(unsigned int i) const { return reinterpret_cast<const T*>(storage_.buf + i * sizeof(T)); }

    XSTD_INLINE void copy_from(const ring_buffer& other) {
        for (unsigned int i = 0; i < other.size_; ++i) {
            unsigned int src_idx = index(other.head_ + i);
            new (raw(i)) T(*other.ptr_const(src_idx));
        }
        head_ = 0;
        size_ = other.size_;
    }

    union storage_t {
        char   buf[sizeof(T) * N];
        double align;
    } storage_;
    unsigned int head_;
    unsigned int size_;
};

} // namespace xstd

#endif // XSTD_RING_BUFFER_H
