// ============================================================================
//  xstd/small_vector.h
//  ---------------------------------------------------------------------------
//  Hybrid vector: stores up to N elements inline (no allocation), then spills
//  to malloc'd heap once size > N. Useful when most call sites stay small.
//
//  Provides:
//    - xstd::small_vector<T, N>
//        same surface as xstd::vector<T> (push_back / pop_back / emplace /
//        reserve / resize / erase / iteration), plus capacity() reports
//        whichever storage is active.
//
//        bool         is_heap() const              — true once spilled
//
//    - xstd::move(small_vector<T,N>&) -> detail::small_vector_move_proxy<T,N>
//
//  Notes:
//    - Spilling is one-way. Once heap, stays heap until destruction.
//    - shrink_to_fit() will release the heap buffer and move elements back
//      inline if size() <= N.
//    - OOM model matches xstd::vector — silent.
//    - Move via xstd::move() does:
//        * heap source : pointer-swap (cheap, no copies).
//        * inline source: per-element placement-new copy into the destination's
//          inline buffer, then destroy source elements. No allocation.
//    - T must have natural alignment <= 8.
// ============================================================================

#ifndef XSTD_SMALL_VECTOR_H
#define XSTD_SMALL_VECTOR_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include <stdlib.h>
#include "assert.h"

namespace xstd {

template <typename T, unsigned int N> class small_vector;

namespace detail {

template <typename T, unsigned int N>
struct small_vector_move_proxy {
    small_vector<T, N>* src;
    XSTD_INLINE explicit small_vector_move_proxy(small_vector<T, N>& s) : src(&s) {}
};

} // namespace detail

template <typename T, unsigned int N>
class small_vector {
public:
    XSTD_INLINE small_vector()
        : heap_(XSTD_NULL), size_(0), capacity_(N), is_heap_(false) {}

    XSTD_INLINE small_vector(const small_vector& other)
        : heap_(XSTD_NULL), size_(0), capacity_(N), is_heap_(false) {
        copy_from(other);
    }

    XSTD_INLINE small_vector(const detail::small_vector_move_proxy<T, N>& p)
        : heap_(XSTD_NULL), size_(0), capacity_(N), is_heap_(false) {
        small_vector& s = *p.src;
        if (s.is_heap_) {
            // steal heap buffer
            heap_     = s.heap_;
            size_     = s.size_;
            capacity_ = s.capacity_;
            is_heap_  = true;
            s.heap_     = XSTD_NULL;
            s.size_     = 0;
            s.capacity_ = N;
            s.is_heap_  = false;
        } else {
            // relocate elements bit-for-bit into our inline buffer
            for (unsigned int i = 0; i < s.size_; ++i) {
                const char* src_byte = s.inline_buf_ + i * sizeof(T);
                char*       dst_byte = inline_buf_   + i * sizeof(T);
                for (unsigned int b = 0; b < sizeof(T); ++b) dst_byte[b] = src_byte[b];
            }
            size_ = s.size_;
            s.size_ = 0; // source forgets — no dtors on relocated bytes
        }
    }

    XSTD_INLINE ~small_vector() {
        destroy_all();
        if (is_heap_ && heap_) free(heap_);
    }

    XSTD_INLINE small_vector& operator=(const small_vector& other) {
        if (this == &other) return *this;
        clear();
        copy_from(other);
        return *this;
    }

    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return capacity_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }
    XSTD_INLINE bool         is_heap() const { return is_heap_; }

    XSTD_INLINE T*       data()       { return slot_ptr(); }
    XSTD_INLINE const T* data() const { return slot_ptr_const(); }

    XSTD_INLINE T&       operator[](unsigned int i)       { XSTD_ASSERT(i < size_); return slot_ptr()[i]; }
    XSTD_INLINE const T& operator[](unsigned int i) const { XSTD_ASSERT(i < size_); return slot_ptr_const()[i]; }

    XSTD_INLINE T&       front()       { XSTD_ASSERT(size_ > 0); return slot_ptr()[0]; }
    XSTD_INLINE const T& front() const { XSTD_ASSERT(size_ > 0); return slot_ptr_const()[0]; }
    XSTD_INLINE T&       back()        { XSTD_ASSERT(size_ > 0); return slot_ptr()[size_ - 1]; }
    XSTD_INLINE const T& back()  const { XSTD_ASSERT(size_ > 0); return slot_ptr_const()[size_ - 1]; }

    XSTD_INLINE T*       begin()       { return slot_ptr(); }
    XSTD_INLINE const T* begin() const { return slot_ptr_const(); }
    XSTD_INLINE T*       end()         { return slot_ptr() + size_; }
    XSTD_INLINE const T* end()   const { return slot_ptr_const() + size_; }

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
        T* p = slot_ptr();
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (p + i) T();
        } else {
            for (unsigned int i = n; i < size_; ++i) p[i].~T();
        }
        size_ = n;
    }

    XSTD_INLINE void resize(unsigned int n, const T& fill) {
        if (n > capacity_) grow_to(n);
        T* p = slot_ptr();
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) new (p + i) T(fill);
        } else {
            for (unsigned int i = n; i < size_; ++i) p[i].~T();
        }
        size_ = n;
    }

    XSTD_INLINE void shrink_to_fit() {
        if (!is_heap_) return;
        if (size_ <= N) {
            // move back inline
            T* hp = heap_;
            for (unsigned int i = 0; i < size_; ++i) {
                new (inline_slot(i)) T(hp[i]);
                hp[i].~T();
            }
            free(hp);
            heap_     = XSTD_NULL;
            capacity_ = N;
            is_heap_  = false;
        } else if (size_ < capacity_) {
            T* nb = (T*)malloc(sizeof(T) * size_);
            if (!nb) return;
            for (unsigned int i = 0; i < size_; ++i) {
                new (nb + i) T(heap_[i]);
                heap_[i].~T();
            }
            free(heap_);
            heap_     = nb;
            capacity_ = size_;
        }
    }

    XSTD_INLINE void push_back(const T& v) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        new (slot_ptr() + size_) T(v);
        ++size_;
    }

    XSTD_INLINE void pop_back() {
        XSTD_ASSERT(size_ > 0);
        --size_;
        slot_ptr()[size_].~T();
    }

    XSTD_INLINE void erase(T* it) {
        XSTD_ASSERT(it >= begin() && it < end());
        T* base = slot_ptr();
        unsigned int idx = (unsigned int)(it - base);
        for (unsigned int i = idx; i + 1 < size_; ++i) base[i] = base[i + 1];
        --size_;
        base[size_].~T();
    }

    XSTD_INLINE void erase(T* first, T* last) {
        XSTD_ASSERT(first >= begin() && last <= end() && first <= last);
        T* base = slot_ptr();
        unsigned int a = (unsigned int)(first - base);
        unsigned int b = (unsigned int)(last  - base);
        unsigned int gap = b - a;
        if (gap == 0) return;
        for (unsigned int i = b; i < size_; ++i) base[i - gap] = base[i];
        for (unsigned int i = size_ - gap; i < size_; ++i) base[i].~T();
        size_ -= gap;
    }

    XSTD_INLINE void emplace_back0() {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        new (slot_ptr() + size_) T();
        ++size_;
    }
    template <typename A>
    XSTD_INLINE void emplace_back1(const A& a) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        new (slot_ptr() + size_) T(a);
        ++size_;
    }
    template <typename A, typename B>
    XSTD_INLINE void emplace_back2(const A& a, const B& b) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        new (slot_ptr() + size_) T(a, b);
        ++size_;
    }
    template <typename A, typename B, typename C>
    XSTD_INLINE void emplace_back3(const A& a, const B& b, const C& c) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        new (slot_ptr() + size_) T(a, b, c);
        ++size_;
    }

private:
    XSTD_INLINE T*       slot_ptr()       { return is_heap_ ? heap_ : reinterpret_cast<T*>(inline_buf_); }
    XSTD_INLINE const T* slot_ptr_const() const { return is_heap_ ? heap_ : reinterpret_cast<const T*>(inline_buf_); }
    XSTD_INLINE void*    inline_slot(unsigned int i) { return static_cast<void*>(inline_buf_ + i * sizeof(T)); }

    XSTD_INLINE void destroy_all() {
        T* p = slot_ptr();
        for (unsigned int i = 0; i < size_; ++i) p[i].~T();
    }

    XSTD_INLINE void copy_from(const small_vector& other) {
        if (other.size_ > capacity_) grow_to(other.size_);
        T* p = slot_ptr();
        for (unsigned int i = 0; i < other.size_; ++i) new (p + i) T(other.slot_ptr_const()[i]);
        size_ = other.size_;
    }

    XSTD_INLINE void grow_to(unsigned int needed) {
        unsigned int new_cap = capacity_ * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 8) new_cap = 8;
        T* nb = (T*)malloc(sizeof(T) * new_cap);
        if (!nb) return; // OOM
        T* old = slot_ptr();
        for (unsigned int i = 0; i < size_; ++i) {
            new (nb + i) T(old[i]);
            old[i].~T();
        }
        if (is_heap_ && heap_) free(heap_);
        heap_     = nb;
        capacity_ = new_cap;
        is_heap_  = true;
    }

    // Layout: inline buffer always present; heap pointer only used once spilled.
    union {
        char   inline_buf_[sizeof(T) * N];
        double align_;
    };
    T*           heap_;
    unsigned int size_;
    unsigned int capacity_;
    bool         is_heap_;

    friend struct detail::small_vector_move_proxy<T, N>;
};

template <typename T, unsigned int N>
XSTD_INLINE detail::small_vector_move_proxy<T, N> move(small_vector<T, N>& s) {
    return detail::small_vector_move_proxy<T, N>(s);
}

} // namespace xstd

#endif // XSTD_SMALL_VECTOR_H
