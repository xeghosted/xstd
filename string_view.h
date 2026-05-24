// ============================================================================
//  xstd/string_view.h
//  ---------------------------------------------------------------------------
//  Non-owning view over a contiguous run of `char`. std::string_view-shaped.
//
//  Provides:
//    - xstd::string_view
//        string_view()                              empty
//        string_view(const char* s)                 strlen(s); s may be 0
//        string_view(const char* s, unsigned int n)
//        template <unsigned int N> string_view(const char (&lit)[N])  // length N-1
//
//        const char*  data()  const
//        unsigned int size()  const
//        bool         empty() const
//
//        char         operator[](unsigned int i) const   (XSTD_ASSERT bounds)
//        char         front() const
//        char         back()  const
//
//        const char*  begin() const
//        const char*  end()   const
//
//        string_view  substr(unsigned int off)                              const
//        string_view  substr(unsigned int off, unsigned int count)          const
//
//        bool         starts_with(string_view) const
//        bool         starts_with(char)        const
//        bool         ends_with  (string_view) const
//        bool         ends_with  (char)        const
//        bool         contains   (string_view) const
//        bool         contains   (char)        const
//
//        unsigned int find (char,        unsigned int off = 0) const   // returns npos
//        unsigned int find (string_view, unsigned int off = 0) const
//        unsigned int rfind(char) const
//
//        int          compare(string_view) const   // -1/0/+1
//
//        static const unsigned int npos = (unsigned int)-1;
//
//    - Free operators: == != < <= > >= between two string_views.
//
//  Notes:
//    - View is NOT null-terminated. Do not pass .data() to a C API expecting
//      a C-string unless you know the underlying storage is null-terminated.
//    - Construction from a string literal `"hi"` deduces N=3 and stores
//      length 2 (excludes the trailing NUL).
// ============================================================================

#ifndef XSTD_STRING_VIEW_H
#define XSTD_STRING_VIEW_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <string.h>
#include "assert.h"

namespace xstd {

class string_view {
public:
    static const unsigned int npos = (unsigned int)-1;

    XSTD_INLINE string_view() : data_(XSTD_NULL), size_(0) {}

    XSTD_INLINE string_view(const char* s)
        : data_(s), size_(s ? (unsigned int)strlen(s) : 0) {}

    XSTD_INLINE string_view(const char* s, unsigned int n)
        : data_(s), size_(n) {}

    template <unsigned int N>
    XSTD_INLINE string_view(const char (&lit)[N])
        : data_(lit), size_(N > 0 ? N - 1 : 0) {}

    XSTD_INLINE const char*  data()  const { return data_; }
    XSTD_INLINE unsigned int size()  const { return size_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }

    XSTD_INLINE char operator[](unsigned int i) const {
        XSTD_ASSERT(i < size_);
        return data_[i];
    }

    XSTD_INLINE char front() const { XSTD_ASSERT(size_ > 0); return data_[0]; }
    XSTD_INLINE char back()  const { XSTD_ASSERT(size_ > 0); return data_[size_ - 1]; }

    XSTD_INLINE const char* begin() const { return data_; }
    XSTD_INLINE const char* end()   const { return data_ + size_; }

    XSTD_INLINE string_view substr(unsigned int off) const {
        XSTD_ASSERT(off <= size_);
        return string_view(data_ + off, size_ - off);
    }
    XSTD_INLINE string_view substr(unsigned int off, unsigned int count) const {
        XSTD_ASSERT(off <= size_);
        unsigned int remain = size_ - off;
        if (count > remain) count = remain;
        return string_view(data_ + off, count);
    }

    XSTD_INLINE bool starts_with(string_view p) const {
        if (p.size_ > size_) return false;
        return memcmp(data_, p.data_, p.size_) == 0;
    }
    XSTD_INLINE bool starts_with(char c) const {
        return size_ > 0 && data_[0] == c;
    }
    XSTD_INLINE bool ends_with(string_view p) const {
        if (p.size_ > size_) return false;
        return memcmp(data_ + (size_ - p.size_), p.data_, p.size_) == 0;
    }
    XSTD_INLINE bool ends_with(char c) const {
        return size_ > 0 && data_[size_ - 1] == c;
    }
    XSTD_INLINE bool contains(string_view p) const { return find(p) != npos; }
    XSTD_INLINE bool contains(char c)        const { return find(c) != npos; }

    XSTD_INLINE unsigned int find(char c, unsigned int off = 0) const {
        for (unsigned int i = off; i < size_; ++i) {
            if (data_[i] == c) return i;
        }
        return npos;
    }
    XSTD_INLINE unsigned int find(string_view p, unsigned int off = 0) const {
        if (p.size_ == 0) return off <= size_ ? off : npos;
        if (p.size_ > size_) return npos;
        unsigned int last = size_ - p.size_;
        for (unsigned int i = off; i <= last; ++i) {
            if (memcmp(data_ + i, p.data_, p.size_) == 0) return i;
        }
        return npos;
    }
    XSTD_INLINE unsigned int rfind(char c) const {
        if (size_ == 0) return npos;
        unsigned int i = size_;
        while (i > 0) {
            --i;
            if (data_[i] == c) return i;
        }
        return npos;
    }

    XSTD_INLINE int compare(string_view o) const {
        unsigned int n = size_ < o.size_ ? size_ : o.size_;
        if (n > 0) {
            int c = memcmp(data_, o.data_, n);
            if (c != 0) return c < 0 ? -1 : 1;
        }
        if (size_ < o.size_) return -1;
        if (size_ > o.size_) return  1;
        return 0;
    }

private:
    const char*  data_;
    unsigned int size_;
};

XSTD_INLINE bool operator==(string_view a, string_view b) { return a.compare(b) == 0; }
XSTD_INLINE bool operator!=(string_view a, string_view b) { return a.compare(b) != 0; }
XSTD_INLINE bool operator< (string_view a, string_view b) { return a.compare(b) <  0; }
XSTD_INLINE bool operator<=(string_view a, string_view b) { return a.compare(b) <= 0; }
XSTD_INLINE bool operator> (string_view a, string_view b) { return a.compare(b) >  0; }
XSTD_INLINE bool operator>=(string_view a, string_view b) { return a.compare(b) >= 0; }

} // namespace xstd

#endif // XSTD_STRING_VIEW_H
