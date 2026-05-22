// ============================================================================
//  xstd/call.h
//  ---------------------------------------------------------------------------
//  Type-safe invocation of an absolute function address. Cast an address into
//  a function-pointer of the requested signature and call it in one step.
//
//  Provides:
//    - xstd::call::invoke<R>(address)
//    - xstd::call::invoke<R, P1..Pn>(address, args...)     for n in 1..10
//
//  Usage:
//      typedef int (XamGetCurrentTitleId_t)();   (signature reference only)
//      DWORD title_id = xstd::call::invoke<DWORD>(0x817A1A30);
//
//      // five-arg call
//      int rv = xstd::call::invoke<int, DWORD, void*, int, int, void*>(
//          0x81234567, flags, buffer, size, mode, ctx);
//
//  Notes:
//    - No variadic templates in this dialect, so the API is fixed at 10 args.
//    - The Xbox 360 ABI passes the first eight (or so) integral / pointer args
//      in registers and floats in FPRs; the compiler picks the call sequence
//      based on the signature you give here, so make sure it matches the
//      target function.
// ============================================================================

#ifndef XSTD_CALL_H
#define XSTD_CALL_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

namespace xstd {
namespace call {

template <typename R>
XSTD_INLINE R invoke(unsigned int address) {
    return ((R(*)())address)();
}

template <typename R, typename P1>
XSTD_INLINE R invoke(unsigned int address, P1 p1) {
    return ((R(*)(P1))address)(p1);
}

template <typename R, typename P1, typename P2>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2) {
    return ((R(*)(P1, P2))address)(p1, p2);
}

template <typename R, typename P1, typename P2, typename P3>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3) {
    return ((R(*)(P1, P2, P3))address)(p1, p2, p3);
}

template <typename R, typename P1, typename P2, typename P3, typename P4>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4) {
    return ((R(*)(P1, P2, P3, P4))address)(p1, p2, p3, p4);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) {
    return ((R(*)(P1, P2, P3, P4, P5))address)(p1, p2, p3, p4, p5);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) {
    return ((R(*)(P1, P2, P3, P4, P5, P6))address)(p1, p2, p3, p4, p5, p6);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7) {
    return ((R(*)(P1, P2, P3, P4, P5, P6, P7))address)(p1, p2, p3, p4, p5, p6, p7);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8) {
    return ((R(*)(P1, P2, P3, P4, P5, P6, P7, P8))address)(p1, p2, p3, p4, p5, p6, p7, p8);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8, P9 p9) {
    return ((R(*)(P1, P2, P3, P4, P5, P6, P7, P8, P9))address)(p1, p2, p3, p4, p5, p6, p7, p8, p9);
}

template <typename R, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
XSTD_INLINE R invoke(unsigned int address, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8, P9 p9, P10 p10) {
    return ((R(*)(P1, P2, P3, P4, P5, P6, P7, P8, P9, P10))address)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
}

} // namespace call
} // namespace xstd

#endif // XSTD_CALL_H
