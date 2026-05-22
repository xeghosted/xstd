// ============================================================================
//  xstd/memory.h
//  ---------------------------------------------------------------------------
//  Raw memory read/write/patch helpers for Xbox 360 homebrew. The kernel runs
//  in privileged mode and most addresses are writable, so these are very thin
//  wrappers around pointer casts. PowerPC instructions are 4 bytes and
//  big-endian, which is why nop / branch helpers use 32-bit writes.
//
//  Provides:
//    - xstd::memory::read<T>(addr)              read a T from an absolute addr
//    - xstd::memory::write<T>(addr, value)      write a T to an absolute addr
//    - xstd::memory::write<T>(ptr, value)       same, but with a void*
//    - xstd::memory::copy(dst, src, n)          byte-copy (forward; no overlap)
//    - xstd::memory::fill(dst, byte, n)
//    - xstd::memory::nop(addr)                  write a single PPC `nop`  (4 B)
//    - xstd::memory::nop_range(addr, count)     `count` consecutive PPC nops
//    - xstd::memory::write_branch(addr, target)
//          write a PPC unconditional branch (b target) — 26-bit signed
//          displacement, ±32 MiB range. Returns true on success.
//    - xstd::memory::write_branch_link(addr, target)
//          same but `bl` (sets LR). Useful for inserting a hook call.
//
//  Notes:
//    - No alignment / range checks. Caller must ensure pages are writable
//      (typically true on Xbox 360 in unprotected modes; otherwise use
//      VirtualProtect/XPhysical-allocations as appropriate).
//    - For thread-safe in-place patching, halt other threads first or use
//      a higher-level patcher.
// ============================================================================

#ifndef XSTD_MEMORY_H
#define XSTD_MEMORY_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

namespace xstd {
namespace memory {

// ---------------------------------------------------------------------------
//  read / write
// ---------------------------------------------------------------------------
template <typename T>
XSTD_INLINE T read(unsigned int address) {
    return *reinterpret_cast<T*>(address);
}

template <typename T>
XSTD_INLINE T read(const void* ptr) {
    return *static_cast<const T*>(ptr);
}

template <typename T>
XSTD_INLINE void write(unsigned int address, T value) {
    *reinterpret_cast<T*>(address) = value;
}

template <typename T>
XSTD_INLINE void write(void* ptr, T value) {
    *static_cast<T*>(ptr) = value;
}


// ---------------------------------------------------------------------------
//  byte copy / fill
// ---------------------------------------------------------------------------
XSTD_INLINE void copy(void* dst, const void* src, unsigned int n) {
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; ++i) d[i] = s[i];
}

XSTD_INLINE void fill(void* dst, unsigned char byte, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    for (unsigned int i = 0; i < n; ++i) d[i] = byte;
}


// ---------------------------------------------------------------------------
//  PowerPC instruction patching
//
//  - nop is `60 00 00 00`  (ori r0, r0, 0)
//  - b   is `48 00 00 00 | (disp & 0x03FFFFFC)`        primary opcode 18
//  - bl  is `48 00 00 01 | (disp & 0x03FFFFFC)`        same with LK bit
//    Displacement is signed 26-bit (low 2 bits zero), ±32 MiB from addr.
// ---------------------------------------------------------------------------
static const unsigned int PPC_NOP = 0x60000000u;

XSTD_INLINE void nop(unsigned int address) {
    *reinterpret_cast<unsigned int*>(address) = PPC_NOP;
}

XSTD_INLINE void nop_range(unsigned int address, unsigned int count) {
    unsigned int* p = reinterpret_cast<unsigned int*>(address);
    for (unsigned int i = 0; i < count; ++i) p[i] = PPC_NOP;
}

namespace detail {
    XSTD_INLINE bool encode_branch(unsigned int address,
                                   unsigned int target,
                                   bool link,
                                   unsigned int* out) {
        int disp = (int)target - (int)address;
        // 26-bit signed range, low 2 bits must be zero
        if ((disp & 3) != 0) return false;
        if (disp >= (1 << 25) || disp < -(1 << 25)) return false;
        unsigned int instr = 0x48000000u | ((unsigned int)disp & 0x03FFFFFCu);
        if (link) instr |= 1u;
        *out = instr;
        return true;
    }
}

XSTD_INLINE bool write_branch(unsigned int address, unsigned int target) {
    unsigned int instr;
    if (!detail::encode_branch(address, target, false, &instr)) return false;
    *reinterpret_cast<unsigned int*>(address) = instr;
    return true;
}

XSTD_INLINE bool write_branch_link(unsigned int address, unsigned int target) {
    unsigned int instr;
    if (!detail::encode_branch(address, target, true, &instr)) return false;
    *reinterpret_cast<unsigned int*>(address) = instr;
    return true;
}

} // namespace memory
} // namespace xstd

#endif // XSTD_MEMORY_H
