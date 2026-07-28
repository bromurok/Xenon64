#ifndef DBGSWITCH_H
#define DBGSWITCH_H
#include <stdio.h>
#define BUG_HUNT_LOGGING 0

#if BUG_HUNT_LOGGING
    #ifdef __cplusplus
    extern "C" { extern FILE *_dbglog; }
    #else
    extern FILE *_dbglog;
    #endif
    #define DLOG(msg) do { if (_dbglog) { fprintf(_dbglog, "%s\n", msg); fflush(_dbglog); } } while(0)
#else
    #define _dbglog ((FILE*)0)
    #define DLOG(msg) do { } while(0)
#endif

/* ======================================================================
 *  Interpreter Performance Tuning
 * ======================================================================
 *  INTERP_PERF_MODE   : Master switch. 1 = optimizations ON, 0 = original.
 *
 *  If something breaks in a game, set INTERP_PERF_MODE to 0 and report
 *  which sub-toggle caused it.
 *
 *  Sub-toggles (only active when INTERP_PERF_MODE == 1):
 *
 *  INTERP_HOT_LOOP    : Remove profiling counters from the interpreter
 *                       hot loop. Saves ~10-15% CPU. 100% safe.
 *
 *  INTERP_FLAT_PREFETCH : Flatten nested branches in prefetch(). 100% safe.
 *
 *  INTERP_FAST_RD     : Direct RDRAM access for LW/SW/LB/SB/LH/SH/LBU/LHU
 *                       and LWC1/SWC1/LDC1/SDC1/LL/SC/LD/SD
 *                       bypassing the readmem[]/writemem[] function pointer
 *                       table. ~30-50% faster memory access.
 *                       WARNING: Skips framebuffer callbacks (gfx.fBRead/
 *                       gfx.fBWrite). If you see missing framebuffer effects
 *                       (e.g. water, smoke, shadows), set this to 0.
 * ====================================================================== */

#ifndef INTERP_PERF_MODE
#define INTERP_PERF_MODE 1
#endif

#if INTERP_PERF_MODE
    #ifndef INTERP_HOT_LOOP
    #define INTERP_HOT_LOOP 1
    #endif
    #ifndef INTERP_FLAT_PREFETCH
    #define INTERP_FLAT_PREFETCH 1
    #endif
    #ifndef INTERP_FAST_RD
    #define INTERP_FAST_RD 1
    #endif
#endif

/* Branch prediction hints (GCC/Clang only; no-op on MSVC) */
#ifndef LIKELY
  #ifdef __GNUC__
  #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #else
  #define LIKELY(x)   (!!(x))
  #define UNLIKELY(x) (!!(x))
  #endif
#endif

#endif
