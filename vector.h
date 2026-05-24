// ============================================================================
//  xstd/vector.h
//  ---------------------------------------------------------------------------
//  Heap-backed dynamic array. std::vector-shaped, written in the XDK dialect.
//
//  Provides:
//    - xstd::vector<T>
//        vector()                                  empty (no allocation)
//        vector(unsigned int n)                    n default-constructed
//        vector(unsigned int n, const T& fill)
//        vector(const vector&)                     deep copy
//        vector(const detail::vector_move_proxy<T>&)  bitwise relocate
//        ~vector()
//
//        vector& operator=(const vector&)
//
//        unsigned int size()     const
//        unsigned int capacity() const
//        bool         empty() const
//
//        T*           data();    const T* data() const
//        T&           operator[](unsigned int i)
//        const T&     operator[](unsigned int i) const
//        T&           front();   const T& front() const
//        T&           back();    const T& back()  const
//
//        T*           begin();   const T* begin() const
//        T*           end();     const T* end()   const
//
//        void         clear()
//        void         reserve(unsigned int n)
//        void         resize(unsigned int n)
//        void         resize(unsigned int n, const T& fill)
//        void         shrink_to_fit()
//
//        void         push_back(const T& v)
//        void         pop_back()
//        void         erase(T* it)
//        void         erase(T* first, T* last)
//
//        void         emplace_back0()
//        void         emplace_back1(const A&)
//        void         emplace_back2(const A&, const B&)
//        void         emplace_back3(const A&, const B&, const C&)
//
//    - xstd::move(vector<T>&) -> detail::vector_move_proxy<T>
//
//  Notes:
//    - Allocator: malloc / free. Heap re-grow does a placement-new copy of
//      every live element into the new buffer, then destroys the old.
//      For trivially relocatable T this is wasteful but always correct.
//    - Growth policy: doubles, minimum step of 8 slots.
//    - OOM model matches xstd::string — silent. A failed reserve() leaves
//      capacity unchanged; a failed push_back() drops the value. Observable
//      via size() not advancing.
//    - Move via xstd::move() is a pointer-swap of (data, size, capacity)
//      — never copies elements, never allocates.
//    - T must have natural alignment <= 8 (limitation inherited from the
//      double-aligned buffer used for in-place storage by other xstd types;
//      for vector itself, malloc returns 8-byte-aligned blocks on the XDK
//      CRT, which is fine for any T with alignment <= 8).
// ============================================================================

#ifndef XSTD_VECTOR_H
#define XSTD_VECTOR_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include <stdlib.h>     // malloc / free
#include <string.h>     // memcpy (for trivial relocate path — currently unused)
#include "assert.h"

namespace xstd {

template <typename T> class vector;

namespace detail {

template <typename T>
struct vector_move_proxy {
    T*           data;
    unsigned int size;
    unsigned int capacity;
    XSTD_INLINE vector_move_proxy(T* d, unsigned int s, unsigned int c)
        : data(d), size(s), capacity(c) {}
};

} // namespace detail

template <typename T> detail::vector_move_proxy<T> move(vector<T>& v);

template <typename T>
class vector {
public:
    XSTD_INLINE vector() : data_(XSTD_NULL), size_(0), capacity_(0) {}

    XSTD_INLINE vector(unsigned int n)
        : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (n == 0) return;
        grow_to(n);
        if (!data_) return;
        for (unsigned int i = 0; i < n; ++i) new (data_ + i) T();
        size_ = n;
    }

    XSTD_INLINE vector(unsigned int n, const T& fill)
        : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (n == 0) return;
        grow_to(n);
        if (!data_) return;
        for (unsigned int i = 0; i < n; ++i) new (data_ + i) T(fill);
        size_ = n;
    }

    XSTD_INLINE vector(const vector& other)
        : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (other.size_ == 0) return;
        grow_to(other.size_);
        if (!data_) return;
        for (unsigned int i = 0; i < other.size_; ++i) new (data_ + i) T(other.data_[i]);
        size_ = other.size_;
    }

    XSTD_INLINE vector(const detail::vector_move_proxy<T>& p)
        : data_(p.data), size_(p.size), capacity_(p.capacity) {}

    XSTD_INLINE ~vector() {
        destroy_all();
        if (data_) free(data_);
    }

    XSTD_INLINE vector& operator=(const vector& other) {
        if (this == &other) return *this;
        clear();
        if (other.size_ == 0) return *this;
        if (other.size_ > capacity_) grow_to(other.size_);
        if (!data_) return *this;
        for (unsigned int i = 0; i < other.size_; ++i) new (data_ + i) T(other.data_[i]);
        size_ = other.size_;
        return *this;
    }

    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return capacity_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }

    XSTD_INLINE T*       data()       { return data_; }
    XSTD_INLINE const T* data() const { return data_; }

    XSTD_INLINE T&       operator[](unsigned int i)       { XSTD_ASSERT(i < size_); return data_[i]; }
    XSTD_INLINE const T& operator[](unsigned int i) const { XSTD_ASSERT(i < size_); return data_[i]; }

    XSTD_INLINE T&       front()       { XSTD_ASSERT(size_ > 0); return data_[0]; }
    XSTD_INLINE const T& front() const { XSTD_ASSERT(size_ > 0); return data_[0]; }
    XSTD_INLINE T&       back()        { XSTD_ASSERT(size_ > 0); return data_[size_ - 1]; }
    XSTD_INLINE const T& back()  const { XSTD_ASSERT(size_ > 0); return data_[size_ - 1]; }

    XSTD_INLINE T*       begin()       { return data_; }
    XSTD_INLINE const T* begin() const { return data_; }
    XSTD_INLINE T*       end()         { return data_ + size_; }
    XSTD_INLINE const T* end()   const { return data_ + size_; }

    XSTD_INLINE void clear() {
        destroy_all();
        size_ = 0;
    }

    XSTD_INLINE void reserve(unsigned int n) {
        if (n <= capacity_) return;
        grow_to(n);
    }

    XSTD_INLINE void resize(unsigned int n) {
        if (n > capacity_) grow_to(n);
        if (!data_) return;
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (data_ + i) T();
        } else {
            for (unsigned int i = n; i < size_; ++i) data_[i].~T();
        }
        size_ = n;
    }

    XSTD_INLINE void resize(unsigned int n, const T& fill) {
        if (n > capacity_) grow_to(n);
        if (!data_) return;
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (data_ + i) T(fill);
        } else {
            for (unsigned int i = n; i < size_; ++i) data_[i].~T();
        }
        size_ = n;
    }

    XSTD_INLINE void shrink_to_fit() {
        if (size_ == capacity_) return;
        if (size_ == 0) {
            if (data_) free(data_);
            data_ = XSTD_NULL;
            capacity_ = 0;
            return;
        }
        T* nb = (T*)malloc(sizeof(T) * size_);
        if (!nb) return;
        for (unsigned int i = 0; i < size_; ++i) {
            new (nb + i) T(data_[i]);
            data_[i].~T();
        }
        free(data_);
        data_     = nb;
        capacity_ = size_;
    }

    XSTD_INLINE void push_back(const T& v) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return; // OOM
        new (data_ + size_) T(v);
        ++size_;
    }

    XSTD_INLINE void pop_back() {
        XSTD_ASSERT(size_ > 0);
        --size_;
        data_[size_].~T();
    }

    XSTD_INLINE void erase(T* it) {
        XSTD_ASSERT(it >= begin() && it < end());
        unsigned int idx = (unsigned int)(it - data_);
        for (unsigned int i = idx; i + 1 < size_; ++i) {
            data_[i] = data_[i + 1];
        }
        --size_;
        data_[size_].~T();
    }

    XSTD_INLINE void erase(T* first, T* last) {
        XSTD_ASSERT(first >= begin() && last <= end() && first <= last);
        unsigned int a = (unsigned int)(first - data_);
        unsigned int b = (unsigned int)(last  - data_);
        unsigned int gap = b - a;
        if (gap == 0) return;
        for (unsigned int i = b; i < size_; ++i) data_[i - gap] = data_[i];
        for (unsigned int i = size_ - gap; i < size_; ++i) data_[i].~T();
        size_ -= gap;
    }

    XSTD_INLINE void emplace_back0() {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return;
        new (data_ + size_) T();
        ++size_;
    }
    template <typename A>
    XSTD_INLINE void emplace_back1(const A& a) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return;
        new (data_ + size_) T(a);
        ++size_;
    }
    template <typename A, typename B>
    XSTD_INLINE void emplace_back2(const A& a, const B& b) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return;
        new (data_ + size_) T(a, b);
        ++size_;
    }
    template <typename A, typename B, typename C>
    XSTD_INLINE void emplace_back3(const A& a, const B& b, const C& c) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return;
        new (data_ + size_) T(a, b, c);
        ++size_;
    }

private:
    XSTD_INLINE void destroy_all() {
        for (unsigned int i = 0; i < size_; ++i) data_[i].~T();
    }

    XSTD_INLINE void grow_to(unsigned int needed) {
        unsigned int new_cap = capacity_ ? capacity_ * 2 : 8;
        if (new_cap < needed) new_cap = needed;
        T* nb = (T*)malloc(sizeof(T) * new_cap);
        if (!nb) return; // OOM — keep existing state
        for (unsigned int i = 0; i < size_; ++i) {
            new (nb + i) T(data_[i]);
            data_[i].~T();
        }
        if (data_) free(data_);
        data_     = nb;
        capacity_ = new_cap;
    }

    T*           data_;
    unsigned int size_;
    unsigned int capacity_;

    template <typename U>
    friend detail::vector_move_proxy<U> move(vector<U>& v);
};

template <typename T>
XSTD_INLINE detail::vector_move_proxy<T> move(vector<T>& v) {
    detail::vector_move_proxy<T> p(v.data_, v.size_, v.capacity_);
    v.data_     = XSTD_NULL;
    v.size_     = 0;
    v.capacity_ = 0;
    return p;
}

} // namespace xstd

#endif // XSTD_VECTOR_H
