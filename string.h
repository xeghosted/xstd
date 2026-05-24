// ============================================================================
//  xstd/string.h
//  ---------------------------------------------------------------------------
//  Owning, mutable, heap-backed string. Allocation-light: empty strings hold
//  no heap, no SSO buffer either — a default-constructed `string` allocates
//  nothing. Anything that grows the string goes through malloc/free.
//
//  Provides:
//    - xstd::string
//        string()                                   empty (no allocation)
//        string(const char* s)                      copy
//        string(const char* s, unsigned int n)
//        string(string_view sv)
//        string(const string& other)                deep copy
//        string(const detail::string_move_proxy&)   move-in (bitwise relocate)
//        ~string()
//
//        string& operator=(const string&)
//        string& operator=(const char* s)
//        string& operator=(string_view)
//
//        const char*  c_str() const                 always NUL-terminated; "" if empty
//        const char*  data()  const                 same as c_str()
//        unsigned int size()     const
//        unsigned int length()   const              == size()
//        unsigned int capacity() const              storage bytes minus the NUL
//        bool         empty() const
//
//        char         operator[](unsigned int i) const   (XSTD_ASSERT bounds)
//        char&        operator[](unsigned int i)
//        char         front() const
//        char&        front()
//        char         back()  const
//        char&        back()
//
//        void         clear()
//        void         reserve(unsigned int n)
//        void         resize (unsigned int n, char fill = '\0')
//
//        string&      append    (const char* s)
//        string&      append    (const char* s, unsigned int n)
//        string&      append    (string_view)
//        string&      operator+=(const char* s)
//        string&      operator+=(string_view)
//        string&      operator+=(char c)
//        void         push_back (char c)
//        void         pop_back  ()
//
//        string       substr(unsigned int off)                            const
//        string       substr(unsigned int off, unsigned int count)        const
//
//        int          compare(string_view) const
//
//        operator string_view() const                non-owning view of contents
//
//    - xstd::move(string&) -> detail::string_move_proxy
//
//    - to_string(int / unsigned int / __int64 / unsigned __int64) -> string
//
//    - Free operators: == != < <= > >= between strings, and between string
//      and const char*, both directions.
//
//  Notes:
//    - Growth policy: doubles, minimum step of 16.
//    - Allocator is plain malloc/free. Out-of-memory results in a null
//      buffer staying null — appends after OOM are silently dropped. (We
//      have no exceptions and no `bad_alloc`; this matches the rest of the
//      xstd error model: silent degradation, observable via size() == 0.)
//    - Bitwise move via xstd::move(s) transfers (data, size, capacity) and
//      zeroes the source. Cheap; no allocation involved.
//    - No SSO. Could be added later behind the same public API.
// ============================================================================

#ifndef XSTD_STRING_H
#define XSTD_STRING_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <stdlib.h>     // malloc / free
#include <string.h>     // memcpy / strlen / memcmp
#include <stdio.h>      // sprintf (for to_string)
#include "assert.h"
#include "string_view.h"

namespace xstd {

class string;

namespace detail {
    struct string_move_proxy {
        char*        data;
        unsigned int size;
        unsigned int capacity;
        XSTD_INLINE string_move_proxy(char* d, unsigned int s, unsigned int c)
            : data(d), size(s), capacity(c) {}
    };
}

class string {
public:
    XSTD_INLINE string() : data_(XSTD_NULL), size_(0), capacity_(0) {}

    XSTD_INLINE string(const char* s) : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (s) assign_raw(s, (unsigned int)strlen(s));
    }
    XSTD_INLINE string(const char* s, unsigned int n) : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (s && n) assign_raw(s, n);
    }
    XSTD_INLINE string(string_view sv) : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (sv.size() > 0) assign_raw(sv.data(), sv.size());
    }
    XSTD_INLINE string(const string& other) : data_(XSTD_NULL), size_(0), capacity_(0) {
        if (other.size_ > 0) assign_raw(other.data_, other.size_);
    }
    XSTD_INLINE string(const detail::string_move_proxy& p)
        : data_(p.data), size_(p.size), capacity_(p.capacity) {
        // source has been zeroed by xstd::move()
    }

    XSTD_INLINE ~string() {
        if (data_) free(data_);
    }

    XSTD_INLINE string& operator=(const string& other) {
        if (this == &other) return *this;
        if (other.size_ == 0) { clear(); return *this; }
        assign_raw(other.data_, other.size_);
        return *this;
    }
    XSTD_INLINE string& operator=(const char* s) {
        if (!s || !*s) { clear(); return *this; }
        assign_raw(s, (unsigned int)strlen(s));
        return *this;
    }
    XSTD_INLINE string& operator=(string_view sv) {
        if (sv.size() == 0) { clear(); return *this; }
        assign_raw(sv.data(), sv.size());
        return *this;
    }

    XSTD_INLINE const char*  c_str() const { return data_ ? data_ : ""; }
    XSTD_INLINE const char*  data()  const { return c_str(); }
    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int length()   const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return capacity_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }

    XSTD_INLINE char  operator[](unsigned int i) const { XSTD_ASSERT(i < size_); return data_[i]; }
    XSTD_INLINE char& operator[](unsigned int i)       { XSTD_ASSERT(i < size_); return data_[i]; }
    XSTD_INLINE char  front() const { XSTD_ASSERT(size_ > 0); return data_[0]; }
    XSTD_INLINE char& front()       { XSTD_ASSERT(size_ > 0); return data_[0]; }
    XSTD_INLINE char  back()  const { XSTD_ASSERT(size_ > 0); return data_[size_ - 1]; }
    XSTD_INLINE char& back()        { XSTD_ASSERT(size_ > 0); return data_[size_ - 1]; }

    XSTD_INLINE void clear() {
        size_ = 0;
        if (data_) data_[0] = '\0';
    }

    XSTD_INLINE void reserve(unsigned int n) {
        if (n <= capacity_) return;
        grow_to(n);
    }

    XSTD_INLINE void resize(unsigned int n, char fill = '\0') {
        if (n > capacity_) grow_to(n);
        if (!data_) return; // OOM
        if (n > size_) {
            for (unsigned int i = size_; i < n; ++i) data_[i] = fill;
        }
        size_ = n;
        data_[size_] = '\0';
    }

    XSTD_INLINE string& append(const char* s, unsigned int n) {
        if (!s || n == 0) return *this;
        unsigned int new_size = size_ + n;
        if (new_size > capacity_) grow_to(new_size);
        if (!data_) return *this; // OOM
        memcpy(data_ + size_, s, n);
        size_ = new_size;
        data_[size_] = '\0';
        return *this;
    }
    XSTD_INLINE string& append(const char* s) {
        return s ? append(s, (unsigned int)strlen(s)) : *this;
    }
    XSTD_INLINE string& append(string_view sv) { return append(sv.data(), sv.size()); }

    XSTD_INLINE string& operator+=(const char* s)  { return append(s); }
    XSTD_INLINE string& operator+=(string_view sv) { return append(sv); }
    XSTD_INLINE string& operator+=(char c)         { push_back(c); return *this; }

    XSTD_INLINE void push_back(char c) {
        if (size_ + 1 > capacity_) grow_to(size_ + 1);
        if (!data_) return; // OOM
        data_[size_++] = c;
        data_[size_]   = '\0';
    }

    XSTD_INLINE void pop_back() {
        XSTD_ASSERT(size_ > 0);
        --size_;
        data_[size_] = '\0';
    }

    XSTD_INLINE string substr(unsigned int off) const {
        XSTD_ASSERT(off <= size_);
        return string(data_ + off, size_ - off);
    }
    XSTD_INLINE string substr(unsigned int off, unsigned int count) const {
        XSTD_ASSERT(off <= size_);
        unsigned int remain = size_ - off;
        if (count > remain) count = remain;
        return string(data_ + off, count);
    }

    XSTD_INLINE int compare(string_view sv) const {
        return view().compare(sv);
    }

    XSTD_INLINE operator string_view() const { return view(); }

private:
    XSTD_INLINE string_view view() const {
        return string_view(data_ ? data_ : "", size_);
    }

    XSTD_INLINE void grow_to(unsigned int needed) {
        unsigned int new_cap = capacity_ ? capacity_ * 2 : 16;
        if (new_cap < needed) new_cap = needed;
        // alloc new_cap + 1 to leave room for the NUL
        char* nb = (char*)malloc((size_t)new_cap + 1);
        if (!nb) return; // OOM — keep existing state
        if (size_ > 0 && data_) memcpy(nb, data_, size_);
        nb[size_] = '\0';
        if (data_) free(data_);
        data_     = nb;
        capacity_ = new_cap;
    }

    XSTD_INLINE void assign_raw(const char* s, unsigned int n) {
        if (n > capacity_) grow_to(n);
        if (!data_) return; // OOM
        memcpy(data_, s, n);
        size_ = n;
        data_[size_] = '\0';
    }

    char*        data_;
    unsigned int size_;
    unsigned int capacity_;

    friend detail::string_move_proxy move(string& s);
};

XSTD_INLINE detail::string_move_proxy move(string& s) {
    detail::string_move_proxy p(s.data_, s.size_, s.capacity_);
    s.data_     = XSTD_NULL;
    s.size_     = 0;
    s.capacity_ = 0;
    return p;
}

// --- to_string overloads --------------------------------------------------

XSTD_INLINE string to_string(int v) {
    char buf[16];
    sprintf(buf, "%d", v);
    return string(buf);
}
XSTD_INLINE string to_string(unsigned int v) {
    char buf[16];
    sprintf(buf, "%u", v);
    return string(buf);
}
XSTD_INLINE string to_string(__int64 v) {
    char buf[24];
    sprintf(buf, "%I64d", v);
    return string(buf);
}
XSTD_INLINE string to_string(unsigned __int64 v) {
    char buf[24];
    sprintf(buf, "%I64u", v);
    return string(buf);
}

// --- mixed comparisons against const char* --------------------------------

XSTD_INLINE bool operator==(const string& a, const string& b) { return a.compare(b) == 0; }
XSTD_INLINE bool operator!=(const string& a, const string& b) { return a.compare(b) != 0; }
XSTD_INLINE bool operator< (const string& a, const string& b) { return a.compare(b) <  0; }
XSTD_INLINE bool operator<=(const string& a, const string& b) { return a.compare(b) <= 0; }
XSTD_INLINE bool operator> (const string& a, const string& b) { return a.compare(b) >  0; }
XSTD_INLINE bool operator>=(const string& a, const string& b) { return a.compare(b) >= 0; }

XSTD_INLINE bool operator==(const string& a, const char* b) { return a.compare(string_view(b)) == 0; }
XSTD_INLINE bool operator!=(const string& a, const char* b) { return a.compare(string_view(b)) != 0; }
XSTD_INLINE bool operator==(const char* a, const string& b) { return b.compare(string_view(a)) == 0; }
XSTD_INLINE bool operator!=(const char* a, const string& b) { return b.compare(string_view(a)) != 0; }

} // namespace xstd

#endif // XSTD_STRING_H
