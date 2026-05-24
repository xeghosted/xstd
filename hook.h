// ============================================================================
//  xstd/hook.h
//  ---------------------------------------------------------------------------
//  PowerPC long-jump function detour (Xbox 360 Xenon). No distance limits.
//
//  Overwrites the first 4 instructions (16 bytes) of the target with an
//  absolute long-jump to the replacement, and stashes those 4 instructions
//  in a 32-byte heap trampoline that long-jumps back to target+16 so the
//  replacement can still call the original via hook::original_as<Fn>().
//
//  Patch site (16 bytes, written into target):
//    lis    r12, hi16(replacement)
//    ori    r12, r12, lo16(replacement)
//    mtctr  r12
//    bctr
//
//  Trampoline (32 bytes, on malloc'd heap):
//    [0] saved original instruction 0
//    [1] saved original instruction 1
//    [2] saved original instruction 2
//    [3] saved original instruction 3
//    [4] lis    r12, hi16(target+16)
//    [5] ori    r12, r12, lo16(target+16)
//    [6] mtctr  r12
//    [7] bctr
//
//  Provides:
//    - xstd::hook
//        hook()
//        hook(void* target, void* replacement)               install
//        hook(unsigned int target, unsigned int replacement)
//        hook(const detail::hook_move_proxy&)
//        ~hook()                                              auto-uninstall
//
//        bool         install(void* target, void* replacement)
//        bool         install(unsigned int target, unsigned int replacement)
//        void         uninstall()
//        bool         installed() const
//
//        unsigned int target_addr() const
//        void*        original() const                        trampoline ptr
//
//        template <typename Fn>
//        Fn           original_as() const                     reinterpret as Fn
//
//    - xstd::move(hook&) -> detail::hook_move_proxy
//
//  Notes:
//    - The target function must be >= 16 bytes long. Most non-trivial PPC
//      functions are; tiny leaf functions may not be.
//    - The 4 saved instructions are relocated verbatim — they must not be
//      PC-relative (no `b`, `bl`, `bc`, etc. in slots 0..3). No detection.
//      A function prologue normally has `mflr`/`stwu`/`std`/`addi`/etc.,
//      all safe. If you must hook past PC-relative code, install a hook at
//      a later instruction within the function instead.
//    - r12 is used as scratch for the indirect jump. It is volatile under
//      the PPC ABI on Xbox 360, so this is safe across any function-call
//      boundary.
//    - Single-threaded install. Halt other threads if they may be inside
//      the patched region.
//    - Cache flush: __dcbst per 32-byte line + __sync + icbi per line +
//      __isync. The icbi is emitted via an inline-asm naked function so
//      the address argument stays in r3 (PPC ABI) at the icbi instruction.
//      Without icbi, uninstall fails to take effect when the target was
//      hot in I-cache (which is the common case after the hook ran once).
//    - hook is non-copyable. Use xstd::move() to transfer ownership.
//    - Trampolines come from a static 64-entry pool (2 KiB) — `malloc`'d
//      heap on Xbox 360 is not executable, so we use BSS instead (data
//      sections of a loaded module are RWX since the platform has no DEP).
//      The pool is per-translation-unit (function-local static) and bump-
//      allocated: uninstall does not reclaim a slot, since hooks typically
//      live for the program duration. If you need more than 64 hooks in
//      one TU, raise XSTD_HOOK_POOL_SIZE before including this header.
//    - install() returns false if the pool is full or target/replacement
//      are not 4-byte aligned. target is left unmodified on failure.
// ============================================================================

#ifndef XSTD_HOOK_H
#define XSTD_HOOK_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <ppcintrinsics.h>

#ifndef XSTD_HOOK_POOL_SIZE
    #define XSTD_HOOK_POOL_SIZE 64
#endif

namespace xstd {

namespace detail {

// Trampoline pool. Lives in the consumer's BSS — RWX on Xbox 360. malloc'd
// heap is not executable, which is why we cannot use the regular allocator.
// Bump-allocator: install consumes a slot, uninstall releases the target's
// patch site but does not reclaim the slot. Per-translation-unit instance.
XSTD_INLINE void* hook_alloc_trampoline() {
    static unsigned char pool[XSTD_HOOK_POOL_SIZE * 32];
    static unsigned int  next_off = 0;
    if (next_off + 32 > sizeof(pool)) return (void*)0;
    void* p = pool + next_off;
    next_off += 32;
    return p;
}

// icbi for one cache line. The XDK PPC assembler rejects the literal-0
// form `icbi 0, r3` but accepts `icbi r0, r3` (r0 in RA position is
// architecturally treated as the literal value 0). naked = no prologue /
// epilogue, so r3 still holds the caller's `addr` argument when icbi fires.
__declspec(naked) static void hook_icbi_line(const void* /*addr*/) {
    __asm {
        icbi r0, r3
        blr
    }
}

// Flush a range of patched code so the CPU re-fetches it. Cache line = 32 B.
XSTD_INLINE void hook_flush_icache_range(const void* start, unsigned int len) {
    const unsigned int LINE = 32;
    unsigned int s = (unsigned int)start & ~(LINE - 1);
    unsigned int e = ((unsigned int)start + len + LINE - 1) & ~(LINE - 1);
    for (unsigned int p = s; p < e; p += LINE) __dcbst(0, (const void*)p);
    __sync();
    for (unsigned int p = s; p < e; p += LINE) hook_icbi_line((const void*)p);
    __isync();
}

// PPC instruction encoders for the long-jump sequence (lis/ori/mtctr/bctr).
//   lis    rD, IMM       opcode 15 (addis rD, 0, IMM)
//   ori    rA, rS, UIMM  opcode 24
//   mtctr  rS            mtspr SPR=9; SPR field = (lo5<<5 | hi5) = 0x120
//   bctr                 bcctr 20, 0, 0
XSTD_INLINE unsigned int ppc_lis  (unsigned int rD, unsigned int imm16) {
    return 0x3C000000u | (rD << 21) | (imm16 & 0xFFFFu);
}
XSTD_INLINE unsigned int ppc_ori  (unsigned int rA, unsigned int rS, unsigned int uimm16) {
    return 0x60000000u | (rS << 21) | (rA << 16) | (uimm16 & 0xFFFFu);
}
XSTD_INLINE unsigned int ppc_mtctr(unsigned int rS) {
    return 0x7C0003A6u | (rS << 21) | (0x120u << 11);
}
XSTD_INLINE unsigned int ppc_bctr (void) {
    return 0x4E800420u;
}

// Write a 4-instruction long-jump sequence at `dst` that branches to `dest`.
// Uses r12 as scratch.
XSTD_INLINE void hook_write_longjump(unsigned int* dst, unsigned int dest) {
    unsigned int hi = (dest >> 16) & 0xFFFFu;
    unsigned int lo = dest & 0xFFFFu;
    dst[0] = ppc_lis  (12, hi);
    dst[1] = ppc_ori  (12, 12, lo);
    dst[2] = ppc_mtctr(12);
    dst[3] = ppc_bctr ();
}

struct hook_move_proxy {
    unsigned int target;
    unsigned int saved[4];
    void*        trampoline;
    bool         installed;
    XSTD_INLINE hook_move_proxy(unsigned int t, const unsigned int* sv, void* tr, bool inst)
        : target(t), trampoline(tr), installed(inst) {
        saved[0] = sv[0]; saved[1] = sv[1]; saved[2] = sv[2]; saved[3] = sv[3];
    }
};

} // namespace detail

class hook {
public:
    XSTD_INLINE hook()
        : target_(0), trampoline_(XSTD_NULL), installed_(false) {
        saved_[0] = saved_[1] = saved_[2] = saved_[3] = 0;
    }

    XSTD_INLINE hook(unsigned int target_addr, unsigned int replacement_addr)
        : target_(0), trampoline_(XSTD_NULL), installed_(false) {
        saved_[0] = saved_[1] = saved_[2] = saved_[3] = 0;
        install(target_addr, replacement_addr);
    }

    XSTD_INLINE hook(void* target_ptr, void* replacement_ptr)
        : target_(0), trampoline_(XSTD_NULL), installed_(false) {
        saved_[0] = saved_[1] = saved_[2] = saved_[3] = 0;
        install(target_ptr, replacement_ptr);
    }

    XSTD_INLINE hook(const detail::hook_move_proxy& p)
        : target_(p.target), trampoline_(p.trampoline), installed_(p.installed) {
        saved_[0] = p.saved[0]; saved_[1] = p.saved[1];
        saved_[2] = p.saved[2]; saved_[3] = p.saved[3];
    }

    XSTD_INLINE ~hook() {
        uninstall();
    }

    XSTD_INLINE bool install(unsigned int target_addr, unsigned int replacement_addr) {
        if (installed_) return false;
        if ((target_addr & 3) != 0 || (replacement_addr & 3) != 0) return false;

        // Allocate 32-byte trampoline from the static (executable) pool.
        // malloc heap is not executable on Xbox 360, so we cannot use it.
        unsigned int* tramp = (unsigned int*)detail::hook_alloc_trampoline();
        if (!tramp) return false;

        // Save the 4 instructions we are about to overwrite.
        unsigned int* tgt = reinterpret_cast<unsigned int*>(target_addr);
        saved_[0] = tgt[0];
        saved_[1] = tgt[1];
        saved_[2] = tgt[2];
        saved_[3] = tgt[3];

        // Trampoline: copy the originals, then long-jump back past the patch.
        tramp[0] = saved_[0];
        tramp[1] = saved_[1];
        tramp[2] = saved_[2];
        tramp[3] = saved_[3];
        detail::hook_write_longjump(tramp + 4, target_addr + 16);

        // Overwrite the patch site with a long-jump to the replacement.
        detail::hook_write_longjump(tgt, replacement_addr);

        // Flush both regions so the CPU re-fetches the new instructions.
        detail::hook_flush_icache_range(tramp, 32);
        detail::hook_flush_icache_range((const void*)target_addr, 16);

        target_     = target_addr;
        trampoline_ = tramp;
        installed_  = true;
        return true;
    }

    XSTD_INLINE bool install(void* target_ptr, void* replacement_ptr) {
        return install(
            (unsigned int)(unsigned __int64)target_ptr,
            (unsigned int)(unsigned __int64)replacement_ptr
        );
    }

    XSTD_INLINE void uninstall() {
        if (!installed_) return;
        unsigned int* tgt = reinterpret_cast<unsigned int*>(target_);
        tgt[0] = saved_[0];
        tgt[1] = saved_[1];
        tgt[2] = saved_[2];
        tgt[3] = saved_[3];
        detail::hook_flush_icache_range((const void*)target_, 16);
        // trampoline slot stays in the pool (bump-allocator); we just
        // forget about it. Re-install on this hook will get a fresh slot.
        trampoline_ = XSTD_NULL;
        installed_  = false;
    }

    XSTD_INLINE bool         installed()   const { return installed_; }
    XSTD_INLINE unsigned int target_addr() const { return target_; }
    XSTD_INLINE void*        original()    const { return trampoline_; }

    template <typename Fn>
    XSTD_INLINE Fn original_as() const {
        return (Fn)trampoline_;
    }

private:
    // non-copyable
    hook(const hook&);
    hook& operator=(const hook&);

    unsigned int target_;
    unsigned int saved_[4];
    void*        trampoline_;
    bool         installed_;

    friend detail::hook_move_proxy move(hook& h);
};

XSTD_INLINE detail::hook_move_proxy move(hook& h) {
    detail::hook_move_proxy p(h.target_, h.saved_, h.trampoline_, h.installed_);
    h.target_     = 0;
    h.saved_[0]   = 0;
    h.saved_[1]   = 0;
    h.saved_[2]   = 0;
    h.saved_[3]   = 0;
    h.trampoline_ = XSTD_NULL;
    h.installed_  = false;
    return p;
}

} // namespace xstd

#endif // XSTD_HOOK_H
