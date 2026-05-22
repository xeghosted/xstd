// ============================================================================
//  xstd/log.h
//  ---------------------------------------------------------------------------
//  Lightweight leveled logger built from xstd/format.h, xstd/file.h, and
//  xstd/chrono.h. Each call prepends a timestamp and level tag, then writes
//  the line to printf (i.e. UART on devkits / dashlaunch) and optionally to
//  a sink file.
//
//  Output line format:
//      [HH:MM:SS.mmm] [LEVEL] message...\n
//
//  Provides:
//    - xstd::log::level    { trace, debug, info, warn, error, off }
//    - xstd::log::logger   instantiate one per subsystem (or share a global)
//        constructor: logger(level threshold = info)
//        set_level(level), set_file(path)        opens a sink file, append mode
//        set_console(bool)                       mirror to printf (default on)
//        set_timestamps(bool)                    enable/disable HH:MM:SS prefix
//        trace/debug/info/warn/error(fmt, ...)   printf-style
//
//  Macros (optional, brevity helpers — define XSTD_LOG_NO_MACROS to suppress):
//        XSTD_LOG_TRACE(lg, ...) ... XSTD_LOG_ERROR(lg, ...)
//
//  Notes:
//    - Timestamps are wall-clock since steady_clock epoch (process uptime),
//      not calendar time — Xbox 360 doesn't have an easy local-time API and
//      uptime is usually what you want in logs anyway.
//    - Not internally synchronized. If you log from multiple threads, wrap
//      calls in your own mutex or accept interleaved lines (printf is usually
//      atomic per call on this platform).
//    - Internal buffer is 512 bytes; messages truncate silently past that.
// ============================================================================

#ifndef XSTD_LOG_H
#define XSTD_LOG_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#include <stdio.h>
#include <stdarg.h>

#include "format.h"
#include "file.h"
#include "chrono.h"

namespace xstd {
namespace log {

enum level {
    trace,
    debug,
    info,
    warn,
    error,
    off
};

namespace detail {
    XSTD_INLINE const char* level_tag(level lv) {
        switch (lv) {
            case trace: return "TRACE";
            case debug: return "DEBUG";
            case info:  return "INFO ";
            case warn:  return "WARN ";
            case error: return "ERROR";
            default:    return "?????";
        }
    }
}

class logger {
public:
    XSTD_INLINE explicit logger(level threshold = info)
        : m_level(threshold)
        , m_console(true)
        , m_timestamps(true)
    {}

    XSTD_INLINE void set_level(level lv)        { m_level = lv; }
    XSTD_INLINE level get_level() const         { return m_level; }
    XSTD_INLINE void set_console(bool on)       { m_console = on; }
    XSTD_INLINE void set_timestamps(bool on)    { m_timestamps = on; }

    // Open / replace the sink file. Lines are appended; existing content is
    // kept. Returns true on success; on failure the logger keeps writing to
    // the console only.
    XSTD_INLINE bool set_file(const char* path) {
        m_file.close();
        return m_file.open(path, xstd::file::write_append);
    }

    XSTD_INLINE void close_file() { m_file.close(); }

    // ---- log calls -------------------------------------------------------
    XSTD_INLINE void trace_(const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(trace, fmt, a); va_end(a);
    }
    XSTD_INLINE void debug_(const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(debug, fmt, a); va_end(a);
    }
    XSTD_INLINE void info_(const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(info, fmt, a); va_end(a);
    }
    XSTD_INLINE void warn_(const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(warn, fmt, a); va_end(a);
    }
    XSTD_INLINE void error_(const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(error, fmt, a); va_end(a);
    }

    // Also expose with the conventional names where allowed. `error` is a
    // very common identifier, so the trailing-underscore versions above are
    // the recommended call sites.
    XSTD_INLINE void log(level lv, const char* fmt, ...) {
        va_list a; va_start(a, fmt); emit(lv, fmt, a); va_end(a);
    }

private:
    level                m_level;
    bool                 m_console;
    bool                 m_timestamps;
    xstd::file::file     m_file;

    logger(const logger&);
    logger& operator=(const logger&);

    XSTD_INLINE void emit(level lv, const char* fmt, va_list args) {
        if (lv < m_level) return;

        char line[512];
        int  pos = 0;

        if (m_timestamps) {
            // Process uptime via steady_clock, formatted HH:MM:SS.mmm
            __int64 ms_total = xstd::chrono::duration_cast<xstd::chrono::milliseconds>(
                xstd::chrono::steady_clock::now().time_since_epoch()).count();
            unsigned int ms  = (unsigned int)(ms_total % 1000);
            unsigned int sec = (unsigned int)((ms_total / 1000) % 60);
            unsigned int min = (unsigned int)((ms_total / 60000) % 60);
            unsigned int hr  = (unsigned int)(ms_total / 3600000);
            pos += xstd::format::print(line + pos, sizeof(line) - pos,
                "[%02u:%02u:%02u.%03u] ", hr, min, sec, ms);
        }
        pos += xstd::format::print(line + pos, sizeof(line) - pos,
            "[%s] ", detail::level_tag(lv));

        pos += xstd::format::detail::vprint(line + pos, sizeof(line) - pos, fmt, args);

        // Ensure a trailing newline. Always have at least 2 bytes free above
        // because vprint terminates at size-1.
        unsigned int len = (unsigned int)pos;
        if (len + 2 < sizeof(line)) {
            if (len == 0 || line[len - 1] != '\n') {
                line[len++] = '\n';
                line[len]   = 0;
            }
        } else {
            line[sizeof(line) - 2] = '\n';
            line[sizeof(line) - 1] = 0;
            len = sizeof(line) - 1;
        }

        if (m_console) {
            printf("%s", line);
        }
        if (m_file.valid()) {
            m_file.write(line, len);
        }
    }
};

} // namespace log
} // namespace xstd

// ---------------------------------------------------------------------------
//  optional convenience macros
// ---------------------------------------------------------------------------
#ifndef XSTD_LOG_NO_MACROS
    #define XSTD_LOG_TRACE(lg, ...) (lg).trace_(__VA_ARGS__)
    #define XSTD_LOG_DEBUG(lg, ...) (lg).debug_(__VA_ARGS__)
    #define XSTD_LOG_INFO(lg,  ...) (lg).info_ (__VA_ARGS__)
    #define XSTD_LOG_WARN(lg,  ...) (lg).warn_ (__VA_ARGS__)
    #define XSTD_LOG_ERROR(lg, ...) (lg).error_(__VA_ARGS__)
#endif

#endif // XSTD_LOG_H
