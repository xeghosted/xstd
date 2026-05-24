// ============================================================================
//  xstd/assert.h
//  ---------------------------------------------------------------------------
//  Minimal assert wrapper so xstd containers / vocab types have a single
//  customization point. Defaults to the CRT assert() in debug builds, and
//  compiles away to nothing in release.
//
//  Define XSTD_ASSERT before including any xstd header to override.
// ============================================================================

#ifndef XSTD_ASSERT_H
#define XSTD_ASSERT_H

#ifndef XSTD_ASSERT
    #ifdef _DEBUG
        #include <assert.h>
        #define XSTD_ASSERT(expr) assert(expr)
    #else
        #define XSTD_ASSERT(expr) ((void)0)
    #endif
#endif

#endif // XSTD_ASSERT_H
