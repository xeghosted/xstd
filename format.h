// ============================================================================
//  xstd/format.h
//  ---------------------------------------------------------------------------
//  Bounded printf-style formatting. Two flavours:
//
//    1. Caller-owned buffer  (preferred, zero global state, thread-safe by
//       construction):
//
//          char b[128];
//          xstd::format::print(b, sizeof(b), "%s = %d", name, value);
//          puts(b);
//
//       Or with the templated wrapper that gives you size via array decay:
//
//          xstd::format::buffer<128> b;
//          b.print("frame %d / %.2f ms", frame, ms);
//          puts(b.c_str());
//
//    2. One-call temporary  (handy for inline formatting; backed by a
//       caller-supplied scratch buffer):
//
//          char b[128];
//          printf("%s\n", xstd::format::tmp(b, sizeof(b), "x=%d y=%d", x, y));
//
//  Provides:
//    - xstd::format::print(buf, size, fmt, ...)
//          Writes at most size-1 chars + null. Returns the number of chars
//          that would have been written (excluding null) — same convention as
//          C99 snprintf, but the buffer is always null-terminated even on
//          truncation.
//
//    - xstd::format::buffer<N>
//          Stack-allocated wrapper around a char[N]. .print(...) formats
//          into the buffer; .c_str() / .data() / .size() expose it.
//          Non-copyable.
//
//    - xstd::format::tmp(buf, size, fmt, ...)
//          Same semantics as print() but returns `buf` so it can be embedded
//          in another format expression.
//
//  Notes:
//    - Uses the CRT's _vsnprintf. Xbox 360 always has it via <stdio.h>.
//    - No <stdarg.h> portability tricks — XDK supports va_list directly.
//    - No exceptions; truncation is silent (buffer is still null-terminated).
// ============================================================================

#ifndef XSTD_FORMAT_H
#define XSTD_FORMAT_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#include <stdio.h>
#include <stdarg.h>

namespace xstd {
namespace format {

namespace detail {
    // _vsnprintf on this toolchain returns:
    //   >= 0 if the whole string fit (chars written, excluding null)
    //   < 0 (-1) if it was truncated
    // We always null-terminate and report a useful count.
    XSTD_INLINE int vprint(char* buf, unsigned int size, const char* fmt, va_list args) {
        if (size == 0) return 0;
        int n = _vsnprintf(buf, size - 1, fmt, args);
        if (n < 0 || (unsigned int)n >= size) {
            buf[size - 1] = 0;
            return (int)(size - 1);
        }
        buf[n] = 0;
        return n;
    }
}

XSTD_INLINE int print(char* buf, unsigned int size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = detail::vprint(buf, size, fmt, args);
    va_end(args);
    return n;
}

XSTD_INLINE const char* tmp(char* buf, unsigned int size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    detail::vprint(buf, size, fmt, args);
    va_end(args);
    return buf;
}


// ---------------------------------------------------------------------------
//  buffer<N>
// ---------------------------------------------------------------------------
template <unsigned int N>
class buffer {
public:
    XSTD_INLINE buffer() : m_len(0) { m_data[0] = 0; }

    XSTD_INLINE int print(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        m_len = detail::vprint(m_data, N, fmt, args);
        va_end(args);
        return m_len;
    }

    XSTD_INLINE int append(const char* fmt, ...) {
        if (m_len >= (int)N - 1) return 0;
        va_list args;
        va_start(args, fmt);
        int n = detail::vprint(m_data + m_len, N - m_len, fmt, args);
        va_end(args);
        m_len += n;
        if (m_len > (int)N - 1) m_len = (int)N - 1;
        return n;
    }

    XSTD_INLINE void clear() { m_len = 0; m_data[0] = 0; }

    XSTD_INLINE const char*  c_str() const { return m_data; }
    XSTD_INLINE char*        data()        { return m_data; }
    XSTD_INLINE int          size()  const { return m_len; }
    XSTD_INLINE unsigned int capacity() const { return N; }
    XSTD_INLINE bool         empty() const { return m_len == 0; }

private:
    char m_data[N];
    int  m_len;

    buffer(const buffer&);
    buffer& operator=(const buffer&);
};

} // namespace format
} // namespace xstd

#endif // XSTD_FORMAT_H
