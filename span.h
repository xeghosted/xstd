// ============================================================================
//  xstd/span.h
//  ---------------------------------------------------------------------------
//  Non-owning view over a contiguous range. std::span-shaped.
//
//  Provides:
//    - xstd::span<T>
//        span()                                empty
//        span(T* data, unsigned int size)
//        span(T (&arr)[N])                     deduces N
//
//        T*           data()  const
//        unsigned int size()  const
//        bool         empty() const
//
//        T&           operator[](unsigned int i) const   (asserts via XSTD_ASSERT)
//        T&           front() const
//        T&           back()  const
//
//        iterator     begin() const            (T*)
//        iterator     end()   const
//
//        span<T>      first(unsigned int count)              const
//        span<T>      last (unsigned int count)              const
//        span<T>      subspan(unsigned int off)              const
//        span<T>      subspan(unsigned int off, unsigned int count) const
//
//  Notes:
//    - For a read-only view use span<const T>.
//    - Iterator type is a raw pointer — no iterator wrapper class.
//    - Bounds violations trigger XSTD_ASSERT.
// ============================================================================

#ifndef XSTD_SPAN_H
#define XSTD_SPAN_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include "assert.h"

namespace xstd {

template <typename T>
class span {
public:
    typedef T*       iterator;
    typedef const T* const_iterator;

    XSTD_INLINE span() : data_(XSTD_NULL), size_(0) {}

    XSTD_INLINE span(T* data, unsigned int size) : data_(data), size_(size) {}

    template <unsigned int N>
    XSTD_INLINE span(T (&arr)[N]) : data_(arr), size_(N) {}

    XSTD_INLINE T*           data()  const { return data_; }
    XSTD_INLINE unsigned int size()  const { return size_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }

    XSTD_INLINE T& operator[](unsigned int i) const {
        XSTD_ASSERT(i < size_);
        return data_[i];
    }

    XSTD_INLINE T& front() const {
        XSTD_ASSERT(size_ > 0);
        return data_[0];
    }

    XSTD_INLINE T& back() const {
        XSTD_ASSERT(size_ > 0);
        return data_[size_ - 1];
    }

    XSTD_INLINE iterator begin() const { return data_; }
    XSTD_INLINE iterator end()   const { return data_ + size_; }

    XSTD_INLINE span<T> first(unsigned int count) const {
        XSTD_ASSERT(count <= size_);
        return span<T>(data_, count);
    }

    XSTD_INLINE span<T> last(unsigned int count) const {
        XSTD_ASSERT(count <= size_);
        return span<T>(data_ + (size_ - count), count);
    }

    XSTD_INLINE span<T> subspan(unsigned int off) const {
        XSTD_ASSERT(off <= size_);
        return span<T>(data_ + off, size_ - off);
    }

    XSTD_INLINE span<T> subspan(unsigned int off, unsigned int count) const {
        XSTD_ASSERT(off <= size_);
        XSTD_ASSERT(count <= size_ - off);
        return span<T>(data_ + off, count);
    }

private:
    T*           data_;
    unsigned int size_;
};

} // namespace xstd

#endif // XSTD_SPAN_H
