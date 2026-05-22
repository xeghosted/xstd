// ============================================================================
//  xstd/file.h
//  ---------------------------------------------------------------------------
//  RAII wrapper around a Win32 file HANDLE. Complements xstd/filesystem.h
//  (which only deals with metadata) — this one does the actual reading and
//  writing. Closes on destruction unless explicitly released.
//
//  Provides:
//    - xstd::file::file               { HANDLE, owned }
//        open(path, mode)
//        close(), release(), valid(), native()
//        read(buf, n)                 returns bytes read, or -1 on error
//        write(buf, n)                returns bytes written, or -1 on error
//        seek(off, whence)            returns new absolute offset, or -1
//        tell()                       absolute offset, or -1
//        size()                       unsigned __int64
//        flush()
//
//    - enum open_mode:
//        read_existing      — open for reading; fail if file missing
//        write_truncate     — open/create for writing; truncate to 0
//        write_append       — open/create for writing; seek to end
//        read_write         — open existing for read+write
//        create_new         — fail if file already exists
//
//    - enum seek_origin: begin, current, end
//
//    - free helpers:
//        read_all(path, out_buf, out_size)        new[]'d buffer, caller frees
//        write_all(path, data, n)
//        last_error()                             wraps GetLastError
//
//    - explicit move via xstd::file::move(f) -> file_move_proxy
//
//  Non-copyable by design.
// ============================================================================

#ifndef XSTD_FILE_H
#define XSTD_FILE_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#ifndef INVALID_FILE_ATTRIBUTES
    #define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

namespace xstd {
namespace file {

enum open_mode {
    read_existing,
    write_truncate,
    write_append,
    read_write,
    create_new
};

enum seek_origin {
    begin,
    current,
    end
};

namespace detail {
    struct file_move_proxy {
        HANDLE h;
        XSTD_INLINE explicit file_move_proxy(HANDLE x) : h(x) {}
    };

    XSTD_INLINE bool mode_flags(open_mode m,
                                DWORD* access,
                                DWORD* share,
                                DWORD* creation,
                                bool*  seek_end) {
        *share    = FILE_SHARE_READ;
        *seek_end = false;
        switch (m) {
            case read_existing:
                *access   = GENERIC_READ;
                *creation = OPEN_EXISTING;
                return true;
            case write_truncate:
                *access   = GENERIC_WRITE;
                *creation = CREATE_ALWAYS;
                return true;
            case write_append:
                *access   = GENERIC_WRITE;
                *creation = OPEN_ALWAYS;
                *seek_end = true;
                return true;
            case read_write:
                *access   = GENERIC_READ | GENERIC_WRITE;
                *creation = OPEN_EXISTING;
                return true;
            case create_new:
                *access   = GENERIC_READ | GENERIC_WRITE;
                *creation = CREATE_NEW;
                return true;
        }
        return false;
    }
}

class file;
XSTD_INLINE detail::file_move_proxy move(file& f);

class file {
private:
    HANDLE m_handle;

    file(const file&);
    file& operator=(const file&);

    friend detail::file_move_proxy xstd::file::move(file& f);

public:
    XSTD_INLINE file() : m_handle(INVALID_HANDLE_VALUE) {}
    XSTD_INLINE explicit file(HANDLE h) : m_handle(h) {}

    XSTD_INLINE file(detail::file_move_proxy p) : m_handle(p.h) {}
    XSTD_INLINE file& operator=(detail::file_move_proxy p) {
        if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
        m_handle = p.h;
        return *this;
    }

    XSTD_INLINE ~file() { close(); }

    XSTD_INLINE HANDLE native() const { return m_handle; }
    XSTD_INLINE bool   valid()  const { return m_handle != INVALID_HANDLE_VALUE; }

    XSTD_INLINE void close() {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
    }

    XSTD_INLINE HANDLE release() {
        HANDLE h = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return h;
    }

    // ---- open ------------------------------------------------------------
    XSTD_INLINE bool open(const char* path, open_mode mode) {
        close();
        DWORD access, share, creation;
        bool  seek_end;
        if (!detail::mode_flags(mode, &access, &share, &creation, &seek_end))
            return false;

        HANDLE h = CreateFileA(path, access, share, 0,
                               creation, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) return false;

        if (seek_end) {
            LARGE_INTEGER zero;
            zero.QuadPart = 0;
            SetFilePointerEx(h, zero, 0, FILE_END);
        }
        m_handle = h;
        return true;
    }

    // ---- I/O -------------------------------------------------------------
    // Returns bytes transferred, or -1 on error.
    XSTD_INLINE int read(void* buf, unsigned int n) {
        DWORD got = 0;
        if (!ReadFile(m_handle, buf, n, &got, 0)) return -1;
        return (int)got;
    }

    XSTD_INLINE int write(const void* buf, unsigned int n) {
        DWORD sent = 0;
        if (!WriteFile(m_handle, buf, n, &sent, 0)) return -1;
        return (int)sent;
    }

    XSTD_INLINE bool flush() {
        return FlushFileBuffers(m_handle) != 0;
    }

    // ---- positioning -----------------------------------------------------
    XSTD_INLINE __int64 seek(__int64 offset, seek_origin o) {
        LARGE_INTEGER off;
        off.QuadPart = offset;
        LARGE_INTEGER out;
        DWORD method = (o == begin)   ? FILE_BEGIN
                     : (o == current) ? FILE_CURRENT
                                      : FILE_END;
        if (!SetFilePointerEx(m_handle, off, &out, method)) return -1;
        return out.QuadPart;
    }

    XSTD_INLINE __int64 tell() {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        LARGE_INTEGER out;
        if (!SetFilePointerEx(m_handle, zero, &out, FILE_CURRENT)) return -1;
        return out.QuadPart;
    }

    XSTD_INLINE unsigned __int64 size() const {
        LARGE_INTEGER s;
        if (!GetFileSizeEx(m_handle, &s)) return 0;
        return (unsigned __int64)s.QuadPart;
    }
};

XSTD_INLINE detail::file_move_proxy move(file& f) {
    HANDLE h = f.m_handle;
    f.m_handle = INVALID_HANDLE_VALUE;
    return detail::file_move_proxy(h);
}

XSTD_INLINE DWORD last_error() { return GetLastError(); }


// ---------------------------------------------------------------------------
//  free helpers
// ---------------------------------------------------------------------------

// Reads the whole file into a freshly-allocated heap buffer.
// On success: *out_buf points to a new[]'d buffer of *out_size bytes; caller
//             must `delete[]` it. Returns true.
// On failure: *out_buf = 0, *out_size = 0; returns false.
XSTD_INLINE bool read_all(const char* path,
                          unsigned char** out_buf,
                          unsigned int*   out_size) {
    *out_buf  = 0;
    *out_size = 0;

    file f;
    if (!f.open(path, read_existing)) return false;

    unsigned __int64 sz = f.size();
    if (sz > 0xFFFFFFFFu) return false; // > 4 GiB not supported here

    unsigned int n = (unsigned int)sz;
    unsigned char* buf = new unsigned char[n ? n : 1];
    if (n > 0) {
        int got = f.read(buf, n);
        if (got != (int)n) { delete[] buf; return false; }
    }
    *out_buf  = buf;
    *out_size = n;
    return true;
}

// Writes the entire buffer to `path`, truncating any existing file.
XSTD_INLINE bool write_all(const char* path, const void* data, unsigned int n) {
    file f;
    if (!f.open(path, write_truncate)) return false;
    if (n == 0) return true;
    int sent = f.write(data, n);
    return sent == (int)n;
}

} // namespace file
} // namespace xstd

#endif // XSTD_FILE_H
