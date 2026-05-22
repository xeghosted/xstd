// ============================================================================
//  xstd/kernel.h
//  ---------------------------------------------------------------------------
//  Helpers for resolving exports from kernel modules (xam.xex, xboxkrnl.exe,
//  xbdm.xex, ...) by ordinal. Useful for picking up xam / xbdm / kernel
//  routines that aren't declared in the public XDK headers.
//
//  Provides:
//    - xstd::kernel::module_handle(name)        -> HANDLE or 0
//    - xstd::kernel::resolve(name, ordinal)     -> unsigned int (address) or 0
//    - xstd::kernel::resolve(handle, ordinal)   -> same, when you already have
//        the module handle (saves a XexGetModuleHandle call per lookup)
//
//  Combine with xstd::call::invoke<R, ...> to call the resolved function:
//
//      unsigned int addr = xstd::kernel::resolve("xam.xex", 0x195);
//      DWORD tid = xstd::call::invoke<DWORD>(addr);    // XamGetCurrentTitleId
//
//  Notes:
//    - Module names are case-sensitive on Xbox 360. Typical spellings:
//        "xam.xex", "xboxkrnl.exe", "xbdm.xex"
//    - Ordinals differ between dashboard versions. Always validate at runtime
//      and bail if the returned address is 0.
// ============================================================================

#ifndef XSTD_KERNEL_H
#define XSTD_KERNEL_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef _XTL_
    #include <xtl.h>
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

// Older XDK headers spell NT_SUCCESS as a macro; fall back to manual check.
#ifndef NT_SUCCESS
    #define NT_SUCCESS(x) ((long)(x) >= 0)
#endif

namespace xstd {
namespace kernel {

XSTD_INLINE HANDLE module_handle(const char* name) {
    HANDLE h = XSTD_NULL;
    if (!NT_SUCCESS(XexGetModuleHandle((char*)name, &h))) return XSTD_NULL;
    return h;
}

XSTD_INLINE unsigned int resolve(HANDLE module, unsigned int ordinal) {
    if (!module) return 0;
    void* addr = XSTD_NULL;
    if (!NT_SUCCESS(XexGetProcedureAddress(module, ordinal, &addr))) return 0;
    return (unsigned int)addr;
}

XSTD_INLINE unsigned int resolve(const char* module_name, unsigned int ordinal) {
    return resolve(module_handle(module_name), ordinal);
}

} // namespace kernel
} // namespace xstd

#endif // XSTD_KERNEL_H
