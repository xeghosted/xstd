// ============================================================================
//  xstd/optional.h
//  ---------------------------------------------------------------------------
//  std::optional-shaped value-or-empty container, written in the XDK dialect.
//
//  Provides:
//    - xstd::optional<T>
//        optional()                          empty
//        optional(const T&)                  engaged copy
//        optional(const optional&)           deep copy
//        optional(const detail::
//                optional_move_proxy<T>&)    move-in (bitwise relocate)
//        ~optional()                         destroys T if engaged
//
//        optional& operator=(const optional&)
//        optional& operator=(const T&)
//        void      reset()
//
//        bool      has_value() const
//        operator  bool_type() const         (safe-bool idiom)
//
//        T&        value()
//        const T&  value() const
//        T&        operator*()
//        const T&  operator*() const
//        T*        operator->()
//        const T*  operator->() const
//        T         value_or(const T& fallback) const
//
//        void      emplace0()
//        void      emplace1(const A&)
//        void      emplace2(const A&, const B&)
//        void      emplace3(const A&, const B&, const C&)
//
//    - xstd::move(optional<T>&) -> detail::optional_move_proxy<T>
//
//  Notes:
//    - Storage is `union { char buf[sizeof(T)]; double align; }`. Natural
//      alignment up to 8 bytes only. Types requiring >8B alignment (e.g.
//      XMVECTOR / __vector4) are not supported — wrap a pointer instead.
//    - Move via xstd::move is a bitwise relocate: source buffer is copied
//      byte-for-byte into the destination and the source is then marked
//      disengaged without invoking T's destructor. Safe for any trivially
//      relocatable T, which covers essentially every xstd type (including
//      move-only ones built on RAII handles).
//    - dereferencing an empty optional triggers XSTD_ASSERT.
// ============================================================================

#ifndef XSTD_OPTIONAL_H
#define XSTD_OPTIONAL_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include "assert.h"

namespace xstd {

template <typename T> class optional;

namespace detail {

template <typename T>
struct optional_move_proxy {
    optional<T>* src;
    XSTD_INLINE explicit optional_move_proxy(optional<T>& s) : src(&s) {}
};

} // namespace detail

template <typename T>
class optional {
public:
    XSTD_INLINE optional() : engaged_(false) {}

    XSTD_INLINE optional(const T& v) : engaged_(false) {
        new (raw()) T(v);
        engaged_ = true;
    }

    XSTD_INLINE optional(const optional& other) : engaged_(false) {
        if (other.engaged_) {
            new (raw()) T(*other.ptr());
            engaged_ = true;
        }
    }

    XSTD_INLINE optional(const detail::optional_move_proxy<T>& p) : engaged_(false) {
        if (p.src->engaged_) {
            unsigned int i;
            for (i = 0; i < sizeof(T); ++i) {
                storage_.buf[i] = p.src->storage_.buf[i];
            }
            engaged_ = true;
            p.src->engaged_ = false; // source forgets — no dtor
        }
    }

    XSTD_INLINE ~optional() {
        reset();
    }

    XSTD_INLINE optional& operator=(const optional& other) {
        if (this == &other) return *this;
        if (other.engaged_) {
            if (engaged_) {
                *ptr() = *other.ptr();
            } else {
                new (raw()) T(*other.ptr());
                engaged_ = true;
            }
        } else {
            reset();
        }
        return *this;
    }

    XSTD_INLINE optional& operator=(const T& v) {
        if (engaged_) {
            *ptr() = v;
        } else {
            new (raw()) T(v);
            engaged_ = true;
        }
        return *this;
    }

    XSTD_INLINE void reset() {
        if (engaged_) {
            ptr()->~T();
            engaged_ = false;
        }
    }

    XSTD_INLINE bool has_value() const { return engaged_; }

private:
    typedef void (optional::*bool_type)() const;
    void safe_bool_true() const {}
public:
    XSTD_INLINE operator bool_type() const {
        return engaged_ ? &optional::safe_bool_true : (bool_type)0;
    }

    XSTD_INLINE T& value() {
        XSTD_ASSERT(engaged_);
        return *ptr();
    }
    XSTD_INLINE const T& value() const {
        XSTD_ASSERT(engaged_);
        return *ptr();
    }

    XSTD_INLINE T&       operator*()        { return value(); }
    XSTD_INLINE const T& operator*() const  { return value(); }
    XSTD_INLINE T*       operator->()       { XSTD_ASSERT(engaged_); return ptr(); }
    XSTD_INLINE const T* operator->() const { XSTD_ASSERT(engaged_); return ptr(); }

    XSTD_INLINE T value_or(const T& fallback) const {
        return engaged_ ? *ptr() : fallback;
    }

    XSTD_INLINE void emplace0() {
        reset();
        new (raw()) T();
        engaged_ = true;
    }
    template <typename A>
    XSTD_INLINE void emplace1(const A& a) {
        reset();
        new (raw()) T(a);
        engaged_ = true;
    }
    template <typename A, typename B>
    XSTD_INLINE void emplace2(const A& a, const B& b) {
        reset();
        new (raw()) T(a, b);
        engaged_ = true;
    }
    template <typename A, typename B, typename C>
    XSTD_INLINE void emplace3(const A& a, const B& b, const C& c) {
        reset();
        new (raw()) T(a, b, c);
        engaged_ = true;
    }

private:
    XSTD_INLINE T*       ptr()       { return reinterpret_cast<T*>(storage_.buf); }
    XSTD_INLINE const T* ptr() const { return reinterpret_cast<const T*>(storage_.buf); }
    XSTD_INLINE void*    raw()       { return static_cast<void*>(storage_.buf); }

    union storage_t {
        char   buf[sizeof(T)];
        double align;
    } storage_;
    bool engaged_;

    friend struct detail::optional_move_proxy<T>;
};

template <typename T>
XSTD_INLINE detail::optional_move_proxy<T> move(optional<T>& o) {
    return detail::optional_move_proxy<T>(o);
}

} // namespace xstd

#endif // XSTD_OPTIONAL_H
