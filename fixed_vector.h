// ============================================================================
//  xstd/fixed_vector.h
//  ---------------------------------------------------------------------------
//  Stack-allocated, fixed-capacity sequence container. No heap, ever.
//  push_back beyond capacity triggers XSTD_ASSERT.
//
//  Provides:
//    - xstd::fixed_vector<T, N>
//        fixed_vector()                            empty
//        fixed_vector(const fixed_vector&)         deep copy
//        ~fixed_vector()                           destroys live elements
//
//        fixed_vector& operator=(const fixed_vector&)
//
//        unsigned int size()     const
//        unsigned int capacity() const             == N
//        bool         empty() const
//        bool         full()  const
//
//        T*           data()
//        const T*     data()  const
//        T&           operator[](unsigned int i)
//        const T&     operator[](unsigned int i) const
//        T&           front();   const T& front() const
//        T&           back();    const T& back()  const
//
//        T*           begin();   const T* begin() const
//        T*           end();     const T* end()   const
//
//        void         clear()
//        void         push_back(const T& v)        asserts !full()
//        void         pop_back()                   asserts !empty()
//        void         resize(unsigned int n)
//        void         resize(unsigned int n, const T& fill)
//        void         erase(T* it)                 asserts in range
//        void         erase(T* first, T* last)
//
//        void         emplace_back0()
//        void         emplace_back1(const A&)
//        void         emplace_back2(const A&, const B&)
//        void         emplace_back3(const A&, const B&, const C&)
//
//  Notes:
//    - Storage is `char buf[sizeof(T)*N]` aligned to 8 bytes. T must have
//      natural alignment <= 8.
//    - No move proxy — fixed_vector owns no heap, copy ctor is the move.
// ============================================================================

#ifndef XSTD_FIXED_VECTOR_H
#define XSTD_FIXED_VECTOR_H

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
class fixed_vector {
public:
    XSTD_INLINE fixed_vector() : size_(0) {}

    XSTD_INLINE fixed_vector(const fixed_vector& other) : size_(0) {
        for (unsigned int i = 0; i < other.size_; ++i) {
            new (slot(i)) T(other.at_const(i));
        }
        size_ = other.size_;
    }

    XSTD_INLINE ~fixed_vector() { clear(); }

    XSTD_INLINE fixed_vector& operator=(const fixed_vector& other) {
        if (this == &other) return *this;
        clear();
        for (unsigned int i = 0; i < other.size_; ++i) {
            new (slot(i)) T(other.at_const(i));
        }
        size_ = other.size_;
        return *this;
    }

    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return N; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }
    XSTD_INLINE bool         full()  const { return size_ == N; }

    XSTD_INLINE T*       data()       { return ptr(0); }
    XSTD_INLINE const T* data() const { return ptr_const(0); }

    XSTD_INLINE T&       operator[](unsigned int i)       { XSTD_ASSERT(i < size_); return *ptr(i); }
    XSTD_INLINE const T& operator[](unsigned int i) const { XSTD_ASSERT(i < size_); return *ptr_const(i); }

    XSTD_INLINE T&       front()       { XSTD_ASSERT(size_ > 0); return *ptr(0); }
    XSTD_INLINE const T& front() const { XSTD_ASSERT(size_ > 0); return *ptr_const(0); }
    XSTD_INLINE T&       back()        { XSTD_ASSERT(size_ > 0); return *ptr(size_ - 1); }
    XSTD_INLINE const T& back()  const { XSTD_ASSERT(size_ > 0); return *ptr_const(size_ - 1); }

    XSTD_INLINE T*       begin()       { return ptr(0); }
    XSTD_INLINE const T* begin() const { return ptr_const(0); }
    XSTD_INLINE T*       end()         { return ptr(size_); }
    XSTD_INLINE const T* end()   const { return ptr_const(size_); }

    XSTD_INLINE void clear() {
        for (unsigned int i = 0; i < size_; ++i) ptr(i)->~T();
        size_ = 0;
    }

    XSTD_INLINE void push_back(const T& v) {
        XSTD_ASSERT(size_ < N);
        new (slot(size_)) T(v);
        ++size_;
    }

    XSTD_INLINE void pop_back() {
        XSTD_ASSERT(size_ > 0);
        --size_;
        ptr(size_)->~T();
    }

    XSTD_INLINE void resize(unsigned int n) {
        XSTD_ASSERT(n <= N);
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (slot(i)) T();
        } else {
            for (unsigned int i = n; i < size_; ++i) ptr(i)->~T();
        }
        size_ = n;
    }

    XSTD_INLINE void resize(unsigned int n, const T& fill) {
        XSTD_ASSERT(n <= N);
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (slot(i)) T(fill);
        } else {
            for (unsigned int i = n; i < size_; ++i) ptr(i)->~T();
        }
        size_ = n;
    }

    XSTD_INLINE void erase(T* it) {
        XSTD_ASSERT(it >= begin() && it < end());
        unsigned int idx = (unsigned int)(it - begin());
        for (unsigned int i = idx; i + 1 < size_; ++i) {
            *ptr(i) = *ptr(i + 1);
        }
        --size_;
        ptr(size_)->~T();
    }

    XSTD_INLINE void erase(T* first, T* last) {
        XSTD_ASSERT(first >= begin() && last <= end() && first <= last);
        unsigned int a = (unsigned int)(first - begin());
        unsigned int b = (unsigned int)(last  - begin());
        unsigned int gap = b - a;
        if (gap == 0) return;
        for (unsigned int i = b; i < size_; ++i) {
            *ptr(i - gap) = *ptr(i);
        }
        for (unsigned int i = size_ - gap; i < size_; ++i) ptr(i)->~T();
        size_ -= gap;
    }

    XSTD_INLINE void emplace_back0() {
        XSTD_ASSERT(size_ < N);
        new (slot(size_)) T();
        ++size_;
    }
    template <typename A>
    XSTD_INLINE void emplace_back1(const A& a) {
        XSTD_ASSERT(size_ < N);
        new (slot(size_)) T(a);
        ++size_;
    }
    template <typename A, typename B>
    XSTD_INLINE void emplace_back2(const A& a, const B& b) {
        XSTD_ASSERT(size_ < N);
        new (slot(size_)) T(a, b);
        ++size_;
    }
    template <typename A, typename B, typename C>
    XSTD_INLINE void emplace_back3(const A& a, const B& b, const C& c) {
        XSTD_ASSERT(size_ < N);
        new (slot(size_)) T(a, b, c);
        ++size_;
    }

private:
    XSTD_INLINE void*    slot(unsigned int i)       { return static_cast<void*>(storage_.buf + i * sizeof(T)); }
    XSTD_INLINE T*       ptr(unsigned int i)        { return reinterpret_cast<T*>(storage_.buf + i * sizeof(T)); }
    XSTD_INLINE const T* ptr_const(unsigned int i) const { return reinterpret_cast<const T*>(storage_.buf + i * sizeof(T)); }
    XSTD_INLINE const T& at_const(unsigned int i)  const { return *ptr_const(i); }

    union storage_t {
        char   buf[sizeof(T) * N];
        double align;
    } storage_;
    unsigned int size_;
};

} // namespace xstd

#endif // XSTD_FIXED_VECTOR_H
