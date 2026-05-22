// ============================================================================
//  xstd/chrono.h
//  ---------------------------------------------------------------------------
//  std::chrono-shaped API for the Xbox 360 XDK. Single-header, matches the
//  spelling of <chrono>: ratio + duration + time_point + clocks + helpers.
//
//  Provides:
//    - xstd::chrono::ratio<Num, Den>
//    - xstd::chrono::duration<Rep, Period>
//        nanoseconds / microseconds / milliseconds / seconds / minutes / hours
//        +, -, *, /, +=, -=, *=, /=, ==, !=, <, <=, >, >=, unary - and +
//    - xstd::chrono::duration_cast<ToDuration>(d)
//        proper cross-multiplied conversion (no precision loss for the
//        ns/us/ms/s/min/h family)
//    - xstd::chrono::time_point<Clock, Duration>
//    - xstd::chrono::system_clock        millisecond wall-clock-ish
//    - xstd::chrono::steady_clock        nanoseconds from QueryPerformanceCounter
//    - xstd::chrono::high_resolution_clock  alias for steady_clock
//    - xstd::chrono::sleep_for(ms)       wraps Win32 Sleep
//    - xstd::chrono::stopwatch           start/stop/reset/elapsed_*
//    - xstd::chrono::game_timer          delta-time + frame-rate limiter
//
//  Notes:
//    - steady_clock::now() splits the QPC math (q/freq)*1e9 + (q%freq)*1e9/freq
//      so the intermediate product never overflows __int64. The naive
//      `counter * 1e9` overflows at ~2.9 seconds on a 3.2 GHz timebase.
//    - Sleep granularity on Xbox 360 is ~1 ms.
//    - This header pulls in <xtl.h> for Sleep + QPC. No other xstd headers
//      are required.
// ============================================================================

#ifndef XSTD_CHRONO_H
#define XSTD_CHRONO_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

namespace xstd {
namespace chrono {

// ---------------------------------------------------------------------------
//  ratio<Num, Den>
// ---------------------------------------------------------------------------
template <__int64 Num, __int64 Den>
struct ratio {
    static const __int64 num = Num;
    static const __int64 den = Den;
};

// Common period ratios (seconds is the base, so num/den scales count -> seconds)
typedef ratio<1, 1000000000> nano;
typedef ratio<1, 1000000>    micro;
typedef ratio<1, 1000>       milli;
typedef ratio<1, 1>          ratio_seconds;
typedef ratio<60, 1>         ratio_minutes;
typedef ratio<3600, 1>       ratio_hours;


// ---------------------------------------------------------------------------
//  duration<Rep, Period>
// ---------------------------------------------------------------------------
template <typename Rep, typename Period>
class duration {
public:
    typedef Rep    rep;
    typedef Period period;

    XSTD_INLINE duration() : m_count(0) {}
    XSTD_INLINE explicit duration(const rep& r) : m_count(r) {}
    XSTD_INLINE duration(const duration& other) : m_count(other.m_count) {}

    XSTD_INLINE duration& operator=(const duration& other) {
        m_count = other.m_count;
        return *this;
    }

    XSTD_INLINE rep count() const { return m_count; }

    XSTD_INLINE duration  operator+() const { return *this; }
    XSTD_INLINE duration  operator-() const { return duration(-m_count); }

    XSTD_INLINE duration& operator++()    { ++m_count; return *this; }
    XSTD_INLINE duration  operator++(int) { duration t(*this); ++m_count; return t; }
    XSTD_INLINE duration& operator--()    { --m_count; return *this; }
    XSTD_INLINE duration  operator--(int) { duration t(*this); --m_count; return t; }

    XSTD_INLINE duration& operator+=(const duration& d) { m_count += d.m_count; return *this; }
    XSTD_INLINE duration& operator-=(const duration& d) { m_count -= d.m_count; return *this; }
    XSTD_INLINE duration& operator*=(const rep& r)      { m_count *= r;         return *this; }
    XSTD_INLINE duration& operator/=(const rep& r)      { m_count /= r;         return *this; }

    XSTD_INLINE bool operator==(const duration& r) const { return m_count == r.m_count; }
    XSTD_INLINE bool operator!=(const duration& r) const { return m_count != r.m_count; }
    XSTD_INLINE bool operator< (const duration& r) const { return m_count <  r.m_count; }
    XSTD_INLINE bool operator<=(const duration& r) const { return m_count <= r.m_count; }
    XSTD_INLINE bool operator> (const duration& r) const { return m_count >  r.m_count; }
    XSTD_INLINE bool operator>=(const duration& r) const { return m_count >= r.m_count; }

    XSTD_INLINE static duration zero() { return duration(0); }

private:
    rep m_count;
};

template <typename Rep, typename Period>
XSTD_INLINE duration<Rep, Period> operator+(const duration<Rep, Period>& a,
                                            const duration<Rep, Period>& b) {
    return duration<Rep, Period>(a.count() + b.count());
}
template <typename Rep, typename Period>
XSTD_INLINE duration<Rep, Period> operator-(const duration<Rep, Period>& a,
                                            const duration<Rep, Period>& b) {
    return duration<Rep, Period>(a.count() - b.count());
}
template <typename Rep, typename Period>
XSTD_INLINE duration<Rep, Period> operator*(const duration<Rep, Period>& d, const Rep& s) {
    return duration<Rep, Period>(d.count() * s);
}
template <typename Rep, typename Period>
XSTD_INLINE duration<Rep, Period> operator*(const Rep& s, const duration<Rep, Period>& d) {
    return duration<Rep, Period>(s * d.count());
}
template <typename Rep, typename Period>
XSTD_INLINE duration<Rep, Period> operator/(const duration<Rep, Period>& d, const Rep& s) {
    return duration<Rep, Period>(d.count() / s);
}


// ---------------------------------------------------------------------------
//  Common duration typedefs
// ---------------------------------------------------------------------------
typedef duration<__int64, nano>          nanoseconds;
typedef duration<__int64, micro>         microseconds;
typedef duration<__int64, milli>         milliseconds;
typedef duration<__int64, ratio_seconds> seconds;
typedef duration<int,     ratio_minutes> minutes;
typedef duration<int,     ratio_hours>   hours;


// ---------------------------------------------------------------------------
//  duration_cast
//
//  Cross-multiply: ToCount = FromCount * (FromPeriod / ToPeriod)
//                          = FromCount * (FromNum * ToDen) / (FromDen * ToNum)
// ---------------------------------------------------------------------------
template <typename ToDuration, typename Rep, typename Period>
XSTD_INLINE ToDuration duration_cast(const duration<Rep, Period>& d) {
    typedef typename ToDuration::rep    to_rep;
    typedef typename ToDuration::period to_period;

    const __int64 mul = (__int64)Period::num * (__int64)to_period::den;
    const __int64 div = (__int64)Period::den * (__int64)to_period::num;
    __int64 v = (__int64)d.count() * mul / div;
    return ToDuration((to_rep)v);
}


// ---------------------------------------------------------------------------
//  time_point<Clock, Duration>
// ---------------------------------------------------------------------------
template <typename Clock, typename Duration>
class time_point {
public:
    typedef Clock                       clock;
    typedef Duration                    duration_t;
    typedef typename Duration::rep      rep;
    typedef typename Duration::period   period;

    XSTD_INLINE time_point() : m_d(Duration(0)) {}
    XSTD_INLINE explicit time_point(const Duration& d) : m_d(d) {}
    XSTD_INLINE time_point(const time_point& other) : m_d(other.m_d) {}

    XSTD_INLINE time_point& operator=(const time_point& other) {
        m_d = other.m_d;
        return *this;
    }

    XSTD_INLINE Duration time_since_epoch() const { return m_d; }

    XSTD_INLINE time_point& operator+=(const Duration& d) { m_d += d; return *this; }
    XSTD_INLINE time_point& operator-=(const Duration& d) { m_d -= d; return *this; }

    XSTD_INLINE bool operator==(const time_point& r) const { return m_d == r.m_d; }
    XSTD_INLINE bool operator!=(const time_point& r) const { return m_d != r.m_d; }
    XSTD_INLINE bool operator< (const time_point& r) const { return m_d <  r.m_d; }
    XSTD_INLINE bool operator<=(const time_point& r) const { return m_d <= r.m_d; }
    XSTD_INLINE bool operator> (const time_point& r) const { return m_d >  r.m_d; }
    XSTD_INLINE bool operator>=(const time_point& r) const { return m_d >= r.m_d; }

private:
    Duration m_d;
};

template <typename Clock, typename Duration>
XSTD_INLINE time_point<Clock, Duration> operator+(const time_point<Clock, Duration>& t,
                                                  const Duration& d) {
    return time_point<Clock, Duration>(t.time_since_epoch() + d);
}
template <typename Clock, typename Duration>
XSTD_INLINE time_point<Clock, Duration> operator+(const Duration& d,
                                                  const time_point<Clock, Duration>& t) {
    return time_point<Clock, Duration>(d + t.time_since_epoch());
}
template <typename Clock, typename Duration>
XSTD_INLINE time_point<Clock, Duration> operator-(const time_point<Clock, Duration>& t,
                                                  const Duration& d) {
    return time_point<Clock, Duration>(t.time_since_epoch() - d);
}
template <typename Clock, typename Duration>
XSTD_INLINE Duration operator-(const time_point<Clock, Duration>& a,
                               const time_point<Clock, Duration>& b) {
    return a.time_since_epoch() - b.time_since_epoch();
}


// ---------------------------------------------------------------------------
//  Clocks
//
//  Both clocks read QueryPerformanceCounter. The math is split as
//      seconds = q / freq, leftover = q % freq
//      ns = seconds * 1e9 + leftover * 1e9 / freq
//  so the intermediate never overflows __int64.
// ---------------------------------------------------------------------------
class steady_clock {
public:
    typedef nanoseconds                       duration_t;
    typedef duration_t::rep                   rep;
    typedef duration_t::period                period;
    typedef xstd::chrono::time_point<steady_clock, duration_t> time_point;
    static const bool is_steady = true;

    XSTD_INLINE static time_point now() {
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);

        __int64 q = counter.QuadPart;
        __int64 f = freq.QuadPart;
        __int64 ns = (q / f) * 1000000000LL + ((q % f) * 1000000000LL) / f;
        return time_point(duration_t(ns));
    }
};

class system_clock {
public:
    typedef milliseconds                       duration_t;
    typedef duration_t::rep                    rep;
    typedef duration_t::period                 period;
    typedef xstd::chrono::time_point<system_clock, duration_t> time_point;
    static const bool is_steady = false;

    XSTD_INLINE static time_point now() {
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);

        __int64 q = counter.QuadPart;
        __int64 f = freq.QuadPart;
        __int64 ms = (q / f) * 1000LL + ((q % f) * 1000LL) / f;
        return time_point(duration_t(ms));
    }
};

typedef steady_clock high_resolution_clock;


// ---------------------------------------------------------------------------
//  Sleep
// ---------------------------------------------------------------------------
XSTD_INLINE void sleep_for(const milliseconds& ms) {
    Sleep((DWORD)ms.count());
}


// ---------------------------------------------------------------------------
//  stopwatch
// ---------------------------------------------------------------------------
class stopwatch {
public:
    XSTD_INLINE stopwatch() : m_start(steady_clock::time_point()), m_running(false) {}

    XSTD_INLINE void start() {
        m_start   = steady_clock::now();
        m_running = true;
    }
    XSTD_INLINE void stop()  { m_running = false; }
    XSTD_INLINE void reset() {
        m_start   = steady_clock::now();
        m_running = false;
    }

    XSTD_INLINE bool is_running() const { return m_running; }

    XSTD_INLINE __int64 elapsed_nanoseconds() const {
        if (!m_running) return 0;
        nanoseconds e = steady_clock::now() - m_start;
        return e.count();
    }
    XSTD_INLINE __int64 elapsed_microseconds() const {
        return duration_cast<microseconds>(steady_clock::now() - m_start).count();
    }
    XSTD_INLINE __int64 elapsed_milliseconds() const {
        if (!m_running) return 0;
        return duration_cast<milliseconds>(steady_clock::now() - m_start).count();
    }
    XSTD_INLINE __int64 elapsed_seconds() const {
        if (!m_running) return 0;
        return duration_cast<seconds>(steady_clock::now() - m_start).count();
    }

private:
    steady_clock::time_point m_start;
    bool                     m_running;
};


// ---------------------------------------------------------------------------
//  game_timer
//
//  delta_time()    returns seconds (float) since the last call
//  limit_framerate() sleeps to keep frames at target_fps
// ---------------------------------------------------------------------------
class game_timer {
public:
    XSTD_INLINE explicit game_timer(int target_fps)
        : m_last(steady_clock::now())
        , m_target_ms(target_fps > 0 ? (1000 / target_fps) : 0) {}

    XSTD_INLINE float delta_time() {
        steady_clock::time_point now = steady_clock::now();
        __int64 ns = (now - m_last).count();
        m_last = now;
        return (float)ns / 1000000000.0f;
    }

    XSTD_INLINE void limit_framerate() {
        if (m_target_ms <= 0) return;
        __int64 frame_ms = duration_cast<milliseconds>(steady_clock::now() - m_last).count();
        if (frame_ms < m_target_ms) {
            sleep_for(milliseconds(m_target_ms - frame_ms));
        }
    }

private:
    steady_clock::time_point m_last;
    __int64                  m_target_ms;
};

} // namespace chrono
} // namespace xstd

#endif // XSTD_CHRONO_H
