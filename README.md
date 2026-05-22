# xstd

Modern, std-like threading primitives for the **Xbox 360 XDK** toolchain.

The Xbox 360 SDK ships with Microsoft's mid-2000s Visual C++ targeting PowerPC (Xenon). That compiler predates most of C++11 — no rvalue references, no variadic templates, no `<type_traits>`, no `constexpr`, no `std::thread`, no exceptions in practice. `xstd` fills that gap with a header-only set of primitives shaped after the `std::` API but rewritten in a dialect the XDK compiler actually accepts.

All headers are inline, allocation-light, and validated on real hardware (devkit + dashlaunch).

## Module status

| Module      | Status     | Contents                                                                                              |
|-------------|------------|-------------------------------------------------------------------------------------------------------|
| `threading`  | ✅ Stable   | atomic, mutex, lock_guard, spinlock, once, condition_variable, shared_mutex, semaphore, future, promise, thread (auto-join), async, packaged_task, latch, barrier, stop_token |
| `filesystem` | ✅ Stable   | `path` + exists, is_directory/regular_file/symlink, file_size, create_directory/directories, remove, remove_all, rename, copy_file, copy, move_file, move_folder, directory_iterate, recursive_directory_iterate |
| `chrono`     | ✅ Stable   | `ratio`, `duration` (ns/us/ms/s/min/h), `duration_cast`, `time_point`, `steady_clock`/`system_clock`/`high_resolution_clock`, `sleep_for`, `stopwatch`, `game_timer` (FPS limiter) |

## Installation

`xstd` is header-only. Drop it into the SDK include tree and `#include` from your project.

### Option A — copy into the SDK (recommended)

Copy the `xstd/` folder into your Xbox 360 SDK include path:

```
C:\Program Files (x86)\Microsoft Xbox 360 SDK\include\xbox\xstd\
```

After that, headers are available via:

```cpp
#include <xstd/threading/mutex.h>
#include <xstd/threading/thread.h>
```

### Option B — clone next to your project

```sh
git clone https://github.com/xeghosted/xstd.git
```

Then add the parent directory to your project's additional include paths and include with the same `<xstd/threading/...>` spelling.

### Requirements

- Xbox 360 SDK (XDK) installed
- A project configured for the **Xbox 360** target (Xenon PowerPC, big-endian)
- Visual Studio 2005 / 2010 with the XDK integration

No external dependencies — only the XDK headers (`<xtl.h>`, `<ppcintrinsics.h>`).

## Quick example

```cpp
#include <xstd/threading/thread.h>
#include <xstd/threading/mutex.h>
#include <xstd/threading/lock_guard.h>
#include <xstd/threading/atomic.h>

xstd::mutex                 g_mu;
xstd::atomic<int>           g_counter(0);

static unsigned long worker(void*) {
    for (int i = 0; i < 1000; ++i) {
        xstd::lock_guard<xstd::mutex> lk(g_mu);
        g_counter.fetch_add(1);
    }
    return 0;
}

void run() {
    xstd::thread t1(worker, 0);
    xstd::thread t2(worker, 0);
    // dtor auto-joins (jthread-style)
}
```

## Threading primitives

| Header                  | Provides                                                                |
|-------------------------|-------------------------------------------------------------------------|
| `atomic.h`              | `atomic<T>` for int32/int64/bool/T*, memory_order, atomic_flag, fences  |
| `mutex.h`               | Recursive `mutex` (CRITICAL_SECTION-backed), generic `lock_guard<L>`    |
| `spinlock.h`            | Non-recursive `spinlock` (atomic_flag + SwitchToThread backoff)         |
| `once.h`                | `once_flag` + `call_once(flag, void(*)())`                              |
| `condition_variable.h`  | Schmidt-style emulated CV — `wait`, `wait_for`, `notify_one/all`        |
| `shared_mutex.h`        | Reader/writer lock (writer-preference) + `shared_lock<SM>`              |
| `semaphore.h`           | Counting semaphore — `acquire`, `try_acquire`, `try_acquire_for`        |
| `future.h`              | `promise<T>` + `future<T>` + `shared_future<T>`, error via `set_error`  |
| `thread.h`              | RAII thread — **auto-joins on destruction** (`jthread`-style)           |
| `async.h`               | `async<R>(fn)` returning `future<R>` (always launches a thread)         |
| `packaged_task.h`       | `packaged_task<R>` (no arg-types, just return type)                     |
| `latch.h`               | Single-use countdown latch                                              |
| `barrier.h`             | Reusable barrier with generation counter                                |
| `stop_token.h`          | `stop_source` + `stop_token` (refcounted shared flag, copyable)         |

## Dialect notes

If you read the headers you'll notice some patterns that differ from modern C++:

- **No rvalue references.** Move-only types ship a `detail::*_move_proxy` and an explicit `xstd::move(x)` free function (similar to a poor man's `std::auto_ptr` transfer).
- **No variadic templates.** APIs that would normally take `Args&&...` either accept a void(*) function pointer or are specialized for 0–3 args (e.g. `optional::emplace0/1/2/3`).
- **No `<type_traits>` / `<utility>`.** Anything needed is reimplemented in `detail::`.
- **No exceptions.** Error states are exposed explicitly (`future::set_error(int)`, return codes, `at_end()` flags).
- **No `constexpr` / `static_assert`.** Pre-C++11.
- **Big-endian native.** The platform is PowerPC, so network byte order is the native order. Any little-endian helper does an explicit byte swap.
- **`XSTD_INLINE = __forceinline`.** Trust the compiler to inline; templates would otherwise tank in this toolchain.

## Limitations

- Targets Xbox 360 only. The Win32 surface used (`CRITICAL_SECTION`, `CreateThread`, `CreateSemaphore`) overlaps with desktop Windows, but XNet / XDK-specific bits leak through and this hasn't been ported.
- `condition_variable` is emulated (Xbox 360 predates Vista's native `CONDITION_VARIABLE`). The implementation is correct but pays a small extra cost per notify compared to the modern Windows primitive.
- `thread` auto-joins in its destructor (deliberate deviation from `std::thread`, which would `terminate()`). If you want a detached thread, call `detach()` explicitly.

## License

[0BSD](LICENSE) — do whatever you want with it, no attribution required.
