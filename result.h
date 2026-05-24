// ============================================================================
//  xstd/result.h
//  ---------------------------------------------------------------------------
//  Discriminated union of "value T" or "error E". Stand-in for exceptions in
//  this dialect; pairs well with xstd::future::set_error and the various
//  return-code APIs already in xstd.
//
//  Provides:
//    - xstd::result<T, E>
//        static result ok(const T&)
//        static result err(const E&)
//
//        result(const result&)
//        result& operator=(const result&)
//        ~result()
//
//        bool      is_ok()  const
//        bool      is_err() const
//        operator  bool_type() const         (safe-bool idiom; true iff is_ok)
//
//        T&        value()
//        const T&  value() const
//        E&        error()
//        const E&  error() const
//        T         value_or(const T& fallback) const
//
//  Notes:
//    - Storage is `union { char ok_buf[sizeof(T)]; char err_buf[sizeof(E)]; double align; }`.
//      Natural alignment up to 8 bytes only.
//    - value()/error() assert via XSTD_ASSERT when the wrong arm is queried.
//    - No move proxy on this first pass. Add one (and a result_move_proxy)
//      later if a consumer needs to transfer a move-only T or E.
// ============================================================================

#ifndef XSTD_RESULT_H
#define XSTD_RESULT_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include "assert.h"

namespace xstd {

template <typename T, typename E>
class result {
public:
    XSTD_INLINE static result ok(const T& v) {
        result r;
        new (r.raw_ok()) T(v);
        r.ok_ = true;
        return r;
    }

    XSTD_INLINE static result err(const E& e) {
        result r;
        new (r.raw_err()) E(e);
        r.ok_ = false;
        return r;
    }

    XSTD_INLINE result(const result& other) : ok_(other.ok_) {
        if (ok_) new (raw_ok())  T(*other.ok_ptr());
        else     new (raw_err()) E(*other.err_ptr());
    }

    XSTD_INLINE result& operator=(const result& other) {
        if (this == &other) return *this;
        destroy();
        ok_ = other.ok_;
        if (ok_) new (raw_ok())  T(*other.ok_ptr());
        else     new (raw_err()) E(*other.err_ptr());
        return *this;
    }

    XSTD_INLINE ~result() { destroy(); }

    XSTD_INLINE bool is_ok()  const { return ok_; }
    XSTD_INLINE bool is_err() const { return !ok_; }

private:
    typedef void (result::*bool_type)() const;
    void safe_bool_true() const {}
public:
    XSTD_INLINE operator bool_type() const {
        return ok_ ? &result::safe_bool_true : (bool_type)0;
    }

    XSTD_INLINE T& value() {
        XSTD_ASSERT(ok_);
        return *ok_ptr();
    }
    XSTD_INLINE const T& value() const {
        XSTD_ASSERT(ok_);
        return *ok_ptr();
    }
    XSTD_INLINE E& error() {
        XSTD_ASSERT(!ok_);
        return *err_ptr();
    }
    XSTD_INLINE const E& error() const {
        XSTD_ASSERT(!ok_);
        return *err_ptr();
    }

    XSTD_INLINE T value_or(const T& fallback) const {
        return ok_ ? *ok_ptr() : fallback;
    }

private:
    XSTD_INLINE result() : ok_(false) {} // uninitialized; factories fill in

    XSTD_INLINE void destroy() {
        if (ok_) ok_ptr()->~T();
        else     err_ptr()->~E();
    }

    XSTD_INLINE T*       ok_ptr()        { return reinterpret_cast<T*>(storage_.ok_buf); }
    XSTD_INLINE const T* ok_ptr()  const { return reinterpret_cast<const T*>(storage_.ok_buf); }
    XSTD_INLINE E*       err_ptr()       { return reinterpret_cast<E*>(storage_.err_buf); }
    XSTD_INLINE const E* err_ptr() const { return reinterpret_cast<const E*>(storage_.err_buf); }
    XSTD_INLINE void*    raw_ok()        { return static_cast<void*>(storage_.ok_buf); }
    XSTD_INLINE void*    raw_err()       { return static_cast<void*>(storage_.err_buf); }

    union storage_t {
        char   ok_buf[sizeof(T)];
        char   err_buf[sizeof(E)];
        double align;
    } storage_;
    bool ok_;
};

} // namespace xstd

#endif // XSTD_RESULT_H
