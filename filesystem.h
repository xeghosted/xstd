// ============================================================================
//  xstd/filesystem.h
//  ---------------------------------------------------------------------------
//  std::filesystem-shaped API for the Xbox 360 XDK. Single-header, header-only,
//  matches the layout of <filesystem>: one path class plus a set of free
//  operations in the xstd::filesystem namespace.
//
//  Provides:
//    - xstd::filesystem::path           heap-backed, copyable, null-terminated
//        c_str(), size(), empty()
//        operator/= , operator/         join with platform separator ('\\')
//        filename(), parent_path(), extension(), stem()
//        operator==, operator!=
//
//    - free operations:
//        exists, is_directory, is_regular_file, is_symlink
//        file_size                          -> unsigned __int64
//        create_directory, create_directories
//        remove, remove_all, rename
//        copy_file(from, to, overwrite=false)
//        copy(from, to, recursive=false)
//        move_file(from, to, overwrite=false)
//        move_folder(from, to, overwrite=false)
//
//    - iteration (callback-based, no allocation):
//        typedef void (*dir_visitor)(const path&, void*);
//        directory_iterate(dir, fn, user)            non-recursive
//        recursive_directory_iterate(dir, fn, user)  walks subtree
//        Return false from the visitor pattern is not supported — visit all
//        entries; if the caller wants early-exit, set a flag in user data and
//        ignore subsequent calls.
//
//  Dialect notes (XDK toolchain):
//    - no rvalue references; copy ctors only
//    - no <string>, <vector>, <functional>; everything is char* + Win32
//    - no exceptions; failures are reported as `false` / 0
//    - path separator is '\\' to match Xbox 360 device paths (game:\, D:\, ...)
// ============================================================================

#ifndef XSTD_FILESYSTEM_H
#define XSTD_FILESYSTEM_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {
namespace filesystem {

// ---------------------------------------------------------------------------
//  internal helpers (private to this header)
// ---------------------------------------------------------------------------
namespace detail {

    XSTD_INLINE unsigned int cstr_len(const char* s) {
        unsigned int n = 0;
        if (s) while (s[n]) ++n;
        return n;
    }

    XSTD_INLINE void cstr_copy(char* dst, const char* src, unsigned int n) {
        for (unsigned int i = 0; i < n; ++i) dst[i] = src[i];
    }

    XSTD_INLINE bool cstr_eq(const char* a, const char* b) {
        if (a == b) return true;
        if (!a || !b) return false;
        unsigned int i = 0;
        while (a[i] && a[i] == b[i]) ++i;
        return a[i] == b[i];
    }

} // namespace detail


// ---------------------------------------------------------------------------
//  path
// ---------------------------------------------------------------------------
class path {
public:
    XSTD_INLINE path() : m_data(XSTD_NULL), m_size(0), m_cap(0) {}

    XSTD_INLINE path(const char* s) : m_data(XSTD_NULL), m_size(0), m_cap(0) {
        assign(s, detail::cstr_len(s));
    }

    XSTD_INLINE path(const path& other) : m_data(XSTD_NULL), m_size(0), m_cap(0) {
        assign(other.m_data, other.m_size);
    }

    XSTD_INLINE ~path() { delete[] m_data; }

    XSTD_INLINE path& operator=(const path& other) {
        if (this != &other) assign(other.m_data, other.m_size);
        return *this;
    }

    XSTD_INLINE path& operator=(const char* s) {
        assign(s, detail::cstr_len(s));
        return *this;
    }

    XSTD_INLINE const char*  c_str() const { return m_data ? m_data : ""; }
    XSTD_INLINE unsigned int size()  const { return m_size; }
    XSTD_INLINE bool         empty() const { return m_size == 0; }

    // ---- join ------------------------------------------------------------
    XSTD_INLINE path& operator/=(const path& other) {
        if (other.m_size == 0) return *this;

        bool need_sep = (m_size > 0)
            && m_data[m_size - 1] != '\\'
            && m_data[m_size - 1] != '/';

        unsigned int new_size = m_size + (need_sep ? 1u : 0u) + other.m_size;
        ensure_cap(new_size);

        if (need_sep) m_data[m_size++] = '\\';
        detail::cstr_copy(m_data + m_size, other.m_data, other.m_size);
        m_size = new_size;
        m_data[m_size] = 0;
        return *this;
    }

    XSTD_INLINE friend path operator/(const path& lhs, const path& rhs) {
        path tmp(lhs);
        tmp /= rhs;
        return tmp;
    }

    // ---- decomposition ---------------------------------------------------
    XSTD_INLINE path filename() const {
        unsigned int pos = last_sep_pos();
        if (pos == 0xFFFFFFFFu) return *this;
        return path(m_data + pos + 1);
    }

    XSTD_INLINE path parent_path() const {
        unsigned int pos = last_sep_pos();
        if (pos == 0xFFFFFFFFu) return path();
        path p;
        p.assign(m_data, pos);
        return p;
    }

    XSTD_INLINE path extension() const {
        unsigned int sep = last_sep_pos();
        unsigned int start = (sep == 0xFFFFFFFFu) ? 0 : sep + 1;
        unsigned int dot = 0xFFFFFFFFu;
        for (unsigned int i = start; i < m_size; ++i) {
            if (m_data[i] == '.') dot = i;
        }
        if (dot == 0xFFFFFFFFu || dot == start) return path();
        return path(m_data + dot);
    }

    XSTD_INLINE path stem() const {
        unsigned int sep = last_sep_pos();
        unsigned int start = (sep == 0xFFFFFFFFu) ? 0 : sep + 1;
        unsigned int dot = 0xFFFFFFFFu;
        for (unsigned int i = start; i < m_size; ++i) {
            if (m_data[i] == '.') dot = i;
        }
        unsigned int end = (dot == 0xFFFFFFFFu || dot == start) ? m_size : dot;
        path p;
        p.assign(m_data + start, end - start);
        return p;
    }

    // ---- comparison ------------------------------------------------------
    XSTD_INLINE friend bool operator==(const path& a, const path& b) {
        if (a.m_size != b.m_size) return false;
        for (unsigned int i = 0; i < a.m_size; ++i)
            if (a.m_data[i] != b.m_data[i]) return false;
        return true;
    }
    XSTD_INLINE friend bool operator!=(const path& a, const path& b) {
        return !(a == b);
    }

private:
    char*         m_data;
    unsigned int  m_size;
    unsigned int  m_cap;

    XSTD_INLINE void ensure_cap(unsigned int need) {
        unsigned int want = need + 1; // null terminator
        if (want <= m_cap) return;
        unsigned int c = m_cap ? m_cap : 32;
        while (c < want) c *= 2;
        char* p = new char[c];
        for (unsigned int i = 0; i < m_size; ++i) p[i] = m_data[i];
        p[m_size] = 0;
        delete[] m_data;
        m_data = p;
        m_cap  = c;
    }

    XSTD_INLINE void assign(const char* src, unsigned int n) {
        ensure_cap(n);
        for (unsigned int i = 0; i < n; ++i) m_data[i] = src[i];
        m_size = n;
        if (m_data) m_data[m_size] = 0;
    }

    XSTD_INLINE unsigned int last_sep_pos() const {
        for (unsigned int i = m_size; i > 0; --i) {
            char c = m_data[i - 1];
            if (c == '\\' || c == '/') return i - 1;
        }
        return 0xFFFFFFFFu;
    }
};


// ---------------------------------------------------------------------------
//  status queries
// ---------------------------------------------------------------------------
XSTD_INLINE bool exists(const path& p) {
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

XSTD_INLINE bool is_directory(const path& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

XSTD_INLINE bool is_regular_file(const path& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

XSTD_INLINE bool is_symlink(const path& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

XSTD_INLINE unsigned __int64 file_size(const path& p) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &info)) return 0;
    LARGE_INTEGER li;
    li.HighPart = info.nFileSizeHigh;
    li.LowPart  = info.nFileSizeLow;
    return (unsigned __int64)li.QuadPart;
}


// ---------------------------------------------------------------------------
//  directory creation
// ---------------------------------------------------------------------------
XSTD_INLINE bool create_directory(const path& p) {
    if (CreateDirectoryA(p.c_str(), XSTD_NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && is_directory(p);
}

XSTD_INLINE bool create_directories(const path& p) {
    if (p.empty()) return false;
    if (CreateDirectoryA(p.c_str(), XSTD_NULL)) return true;

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS && is_directory(p)) return true;

    if (err == ERROR_PATH_NOT_FOUND) {
        path parent = p.parent_path();
        if (!parent.empty() && create_directories(parent)) {
            return CreateDirectoryA(p.c_str(), XSTD_NULL) != 0
                || (GetLastError() == ERROR_ALREADY_EXISTS && is_directory(p));
        }
    }
    return false;
}


// ---------------------------------------------------------------------------
//  iteration (callback-based)
// ---------------------------------------------------------------------------
typedef void (*dir_visitor)(const path& entry, void* user);

XSTD_INLINE bool directory_iterate(const path& dir, dir_visitor visit, void* user) {
    path search = dir / path("*");
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(search.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        const char* name = ffd.cFileName;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        path child = dir / path(name);
        if (visit) visit(child, user);
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return true;
}

XSTD_INLINE bool recursive_directory_iterate(const path& dir, dir_visitor visit, void* user) {
    path search = dir / path("*");
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(search.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        const char* name = ffd.cFileName;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        path child = dir / path(name);
        if (visit) visit(child, user);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            recursive_directory_iterate(child, visit, user);
        }
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return true;
}


// ---------------------------------------------------------------------------
//  copy / move / remove / rename
// ---------------------------------------------------------------------------
XSTD_INLINE bool copy_file(const path& from, const path& to, bool overwrite) {
    return CopyFileA(from.c_str(), to.c_str(), overwrite ? FALSE : TRUE) != 0;
}

XSTD_INLINE bool copy(const path& from, const path& to, bool recursive) {
    if (!is_directory(from)) return copy_file(from, to, true);

    if (!create_directories(to)) return false;

    path search = from / path("*");
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(search.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    do {
        const char* name = ffd.cFileName;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        path src = from / path(name);
        path dst = to   / path(name);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive && !copy(src, dst, true)) { ok = false; break; }
        } else {
            if (!copy_file(src, dst, true)) { ok = false; break; }
        }
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return ok;
}

XSTD_INLINE bool remove(const path& p) {
    if (is_directory(p)) return RemoveDirectoryA(p.c_str()) != 0;
    return DeleteFileA(p.c_str()) != 0;
}

XSTD_INLINE bool remove_all(const path& p) {
    if (!is_directory(p)) return DeleteFileA(p.c_str()) != 0;

    path search = p / path("*");
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(search.c_str(), &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char* name = ffd.cFileName;
            if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
                continue;
            path child = p / path(name);
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_all(child);
            } else {
                DeleteFileA(child.c_str());
            }
        } while (FindNextFileA(h, &ffd));
        FindClose(h);
    }
    return RemoveDirectoryA(p.c_str()) != 0;
}

XSTD_INLINE bool rename(const path& from, const path& to) {
    return MoveFileA(from.c_str(), to.c_str()) != 0;
}

XSTD_INLINE bool move_file(const path& from, const path& to, bool overwrite) {
    if (!exists(from) || is_directory(from)) return false;
    if (overwrite && exists(to)) DeleteFileA(to.c_str());
    return MoveFileA(from.c_str(), to.c_str()) != 0;
}

XSTD_INLINE bool move_folder(const path& from, const path& to, bool overwrite) {
    if (!is_directory(from)) return false;
    if (exists(to) && !overwrite) return false;

    if (MoveFileA(from.c_str(), to.c_str()) != 0) return true;

    if (!copy(from, to, true)) return false;
    return remove_all(from);
}

} // namespace filesystem
} // namespace xstd

#endif // XSTD_FILESYSTEM_H
