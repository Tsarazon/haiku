# Haiku POSIX Modules Dependencies Analysis
Generated: 2025-08-25 15:58:15
Total modules analyzed: 15

## Summary

| Module | Files | Dependencies | Status |
|--------|-------|-------------|--------|
| crypt | 10 | 1 | ⚠️  1 deps |
| glibc | 346 | 2 | ⚠️  2 deps |
| libstdthreads | 5 | 0 | ✅ Clean |
| locale | 16 | 0 | ✅ Clean |
| malloc | 24 | 2 | ⚠️  2 deps |
| musl | 410 | 1 | ⚠️  1 deps |
| pthread | 15 | 0 | ✅ Clean |
| signal | 26 | 0 | ✅ Clean |
| stdio | 6 | 0 | ✅ Clean |
| stdlib | 19 | 1 | ⚠️  1 deps |
| string | 26 | 0 | ✅ Clean |
| sys | 25 | 0 | ✅ Clean |
| time | 11 | 1 | ⚠️  1 deps |
| unistd | 37 | 1 | ⚠️  1 deps |
| wchar | 48 | 0 | ✅ Clean |

## Detailed Analysis

### crypt
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/crypt`
**Files:** 6 sources, 4 headers

⚠️  **External dependencies detected:**

#### MATH (2 occurrences)

**crypt.cpp:**
- Line 15 (Include): `#include <math.h>`
- Line 178 (Function): `long n = static_cast<long>(pow(2, nLog2));`


### glibc
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/glibc`
**Files:** 254 sources, 92 headers

⚠️  **External dependencies detected:**

#### ICU (39 occurrences)

**regex/regexec.c:**
- Line 1660 (Function): `See update_cur_sifted_state().  */`

**stdio-common/vfscanf.c:**
- Line 91 (Function): `# define ungetc_not_eof(c, s)	((void) (--read_in,			      \`
- Line 123 (Function): `# define ungetc_not_eof(c, s)	((void) (--read_in,			      \`

**stdlib/gmp-impl.h:**
- Line 218 (Function): `#define udiv_qrnnd_preinv(q, r, nh, nl, d, di) \`

**stdlib/longlong.h:**
- Line 51 (Function): `1) umul_ppmm(high_prod, low_prod, multipler, multiplicand) multiplies two`
- Line 58 (Function): `3) udiv_qrnnd(quotient, remainder, high_numerator, low_numerator,`
- Line 113 (Function): `#define umul_ppmm(ph, pl, m0, m1) \`
- Line 124 (Function): `#define udiv_qrnnd(q, r, n1, n0, d) \`
- Line 185 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 216 (Function): `#  define umul_ppmm(xh, xl, a, b)					\`
- Line 240 (Function): `#  define umul_ppmm(xh, xl, a, b)					\`
- Line 270 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 313 (Function): `#define umul_ppmm(xh, xl, m0, m1) \`
- Line 370 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 376 (Function): `#define udiv_qrnnd(q, r, n1, n0, dv) \`
- Line 397 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 461 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 468 (Function): `#define udiv_qrnnd(q, r, n1, n0, d) \`
- Line 487 (Function): `#define umul_ppmm(xh, xl, a, b) \`
- Line 563 (Function): `#define umul_ppmm(wh, wl, u, v) \`
- Line 575 (Function): `#define udiv_qrnnd(q, r, n1, n0, d) \`
- Line 595 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 606 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 622 (Function): `#define udiv_qrnnd(q, r, n1, n0, d) \`
- Line 690 (Function): `#define umul_ppmm(ph, pl, m0, m1) \`
- Line 754 (Function): `#define umul_ppmm(ph, pl, m0, m1) \`
- Line 788 (Function): `#define umul_ppmm(ph, pl, m0, m1) \`
- Line 839 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 886 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 892 (Function): `#define udiv_qrnnd(__q, __r, __n1, __n0, __d) \`
- Line 903 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 909 (Function): `#define udiv_qrnnd(q, r, n1, n0, d) \`
- Line 968 (Function): `#define umul_ppmm(w1, w0, u, v) \`
- Line 1017 (Function): `#define udiv_qrnnd(__q, __r, __n1, __n0, __d) \`
- Line 1082 (Function): `#define umul_ppmm(wh, wl, u, v)						\`
- Line 1135 (Function): `#define umul_ppmm(xh, xl, m0, m1) \`
- Line 1180 (Function): `#define umul_ppmm(xh, xl, m0, m1) \`
- Line 1222 (Function): `#define umul_ppmm(w1, w0, u, v)						\`
- Line 1295 (Function): `#define udiv_qrnnd(q, r, nh, nl, d) \`

#### MATH (132 occurrences)

**arch/generic/ldbl2mpn.c:**
- Line 24 (Include): `#include <math.h>`

**arch/generic/mpn2ldbl.c:**
- Line 23 (Include): `#include <math.h>`

**arch/generic/s_cacos.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 32 (Define): `__real__ res = (double) M_PI_2 - __real__ y;`

**arch/generic/s_cacosf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 32 (Define): `__real__ res = (float) M_PI_2 - __real__ y;`

**arch/generic/s_cacoshl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 36 (Define): `__real__ res = HUGE_VALL;`
- Line 43 (Define): `? M_PIl - M_PI_4l : M_PI_4l)`
- Line 44 (Define): `: M_PI_2l), __imag__ x);`
- Line 48 (Define): `__real__ res = HUGE_VALL;`
- Line 51 (Define): `__imag__ res = copysignl (signbit (__real__ x) ? M_PIl : 0.0,`
- Line 65 (Define): `__imag__ res = copysignl (M_PI_2l, __imag__ x);`

**arch/generic/s_cacosl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 32 (Define): `__real__ res = M_PI_2l - __real__ y;`

**arch/generic/s_casin.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 39 (Define): `__imag__ res = copysign (HUGE_VAL, __imag__ x);`

**arch/generic/s_casinf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 39 (Define): `__imag__ res = copysignf (HUGE_VALF, __imag__ x);`

**arch/generic/s_casinh.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 36 (Define): `__real__ res = copysign (HUGE_VAL, __real__ x);`
- Line 41 (Define): `__imag__ res = copysign (rcls >= FP_ZERO ? M_PI_2 : M_PI_4,`

**arch/generic/s_casinhf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 36 (Define): `__real__ res = copysignf (HUGE_VALF, __real__ x);`
- Line 41 (Define): `__imag__ res = copysignf (rcls >= FP_ZERO ? M_PI_2 : M_PI_4,`

**arch/generic/s_casinhl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 36 (Define): `__real__ res = copysignl (HUGE_VALL, __real__ x);`
- Line 41 (Define): `__imag__ res = copysignl (rcls >= FP_ZERO ? M_PI_2l : M_PI_4l,`

**arch/generic/s_casinl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 39 (Define): `__imag__ res = copysignl (HUGE_VALL, __imag__ x);`

**arch/generic/s_ccoshl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`
- Line 67 (Define): `__real__ retval = HUGE_VALL;`
- Line 77 (Define): `__real__ retval = copysignl (HUGE_VALL, cosix);`
- Line 78 (Define): `__imag__ retval = (copysignl (HUGE_VALL, sinix)`
- Line 84 (Define): `__real__ retval = HUGE_VALL;`

**arch/generic/s_cexp.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`
- Line 75 (Define): `double value = signbit (__real__ x) ? 0.0 : HUGE_VAL;`
- Line 95 (Define): `__real__ retval = HUGE_VAL;`

**arch/generic/s_cexpf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`
- Line 75 (Define): `float value = signbit (__real__ x) ? 0.0 : HUGE_VALF;`
- Line 95 (Define): `__real__ retval = HUGE_VALF;`

**arch/generic/s_cexpl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`
- Line 75 (Define): `long double value = signbit (__real__ x) ? 0.0 : HUGE_VALL;`
- Line 95 (Define): `__real__ retval = HUGE_VALL;`

**arch/generic/s_clog.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PI : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VAL;`

**arch/generic/s_clog10.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PI : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VAL;`

**arch/generic/s_clog10f.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PI : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VALF;`

**arch/generic/s_clog10l.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PIl : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VALL;`

**arch/generic/s_clogf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PI : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VALF;`

**arch/generic/s_clogl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`
- Line 37 (Define): `__imag__ result = signbit (__real__ x) ? M_PIl : 0.0;`
- Line 54 (Define): `__real__ result = HUGE_VALL;`

**arch/generic/s_cpow.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`

**arch/generic/s_cpowf.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`

**arch/generic/s_cpowl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <math.h>`

**arch/generic/s_csinhl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`
- Line 86 (Define): `__real__ retval = negate ? -HUGE_VALL : HUGE_VALL;`
- Line 96 (Define): `__real__ retval = copysignl (HUGE_VALL, cosix);`
- Line 97 (Define): `__imag__ retval = copysignl (HUGE_VALL, sinix);`
- Line 105 (Define): `__real__ retval = HUGE_VALL;`

**arch/generic/s_csqrtl.c:**
- Line 22 (Include): `#include <complex.h>`
- Line 23 (Include): `#include <math.h>`
- Line 39 (Define): `__real__ res = HUGE_VALL;`
- Line 47 (Define): `__imag__ res = copysignl (HUGE_VALL, __imag__ x);`

**arch/generic/s_ctanhl.c:**
- Line 21 (Include): `#include <complex.h>`
- Line 22 (Include): `#include <fenv.h>`
- Line 23 (Include): `#include <math.h>`

**include/get-rounding-mode.h:**
- Line 4 (Include): `#include <fenv.h>`

**include/math.h:**
- Line 3 (Include): `#include <math/math.h>`

**include/rounding-mode.h:**
- Line 22 (Include): `#include <fenv.h>`

**math/math.h:**
- Line 19 (Include): `#include <posix/math.h>`
- Line 24 (Define): `#define __HUGE_VALL_bytes	{ 0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f, 0, 0 }`
- Line 27 (Define): `#define HUGE_VALL	(__extension__ \`
- Line 28 (Define): `((__huge_vall_t) { __c: __HUGE_VALL_bytes }).__ld)`
- Line 48 (Function): `extern void sincos(double x, double *sin, double *cos);`

**stdio-common/printf_fp.c:**
- Line 36 (Include): `#include <math.h>`

**stdio-common/printf_fphex.c:**
- Line 22 (Include): `#include <math.h>`

**stdio-common/printf_size.c:**
- Line 23 (Include): `#include <math.h>`

**stdlib/drand48_r.c:**
- Line 21 (Include): `#include <math.h>`

**stdlib/fpioconst.h:**
- Line 24 (Include): `#include <math.h>`

**stdlib/strtod.c:**
- Line 41 (Define): `# define FLOAT_HUGE_VAL	HUGE_VAL`
- Line 60 (Include): `#include <math.h>`
- Line 292 (Define): `return negative ? -FLOAT_HUGE_VAL : FLOAT_HUGE_VAL;`
- Line 433 (Define): `ERANGE and return HUGE_VAL with the appropriate sign.  */`
- Line 591 (Define): `return negative ? -FLOAT_HUGE_VAL : FLOAT_HUGE_VAL;`
- Line 858 (Define): `negative ? -FLOAT_HUGE_VAL : FLOAT_HUGE_VAL);`
- Line 1030 (Define): `return negative ? -FLOAT_HUGE_VAL : FLOAT_HUGE_VAL;`
- Line 1100 (Define): `return negative ? -FLOAT_HUGE_VAL : FLOAT_HUGE_VAL;`

**stdlib/strtof.c:**
- Line 12 (Define): `#define	FLOAT_HUGE_VAL	HUGE_VALF`

**stdlib/strtold.c:**
- Line 18 (Include): `#include <math.h>`

**wcsmbs/wcstof.c:**
- Line 32 (Define): `#define	FLOAT_HUGE_VAL	HUGE_VALF`

**wcsmbs/wcstold.c:**
- Line 20 (Include): `#include <math.h>`
- Line 35 (Define): `# define FLOAT_HUGE_VAL	HUGE_VALL`


### libstdthreads
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/libstdthreads`
**Files:** 5 sources, 0 headers

✅ **No external dependencies found**

### locale
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/locale`
**Files:** 15 sources, 1 headers

✅ **No external dependencies found**

### malloc
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/malloc`
**Files:** 14 sources, 10 headers

⚠️  **External dependencies detected:**

#### ICU (9 occurrences)

**hoard2/wrapper.cpp:**
- Line 322 (Function): `undefer_signals();`
- Line 332 (Function): `undefer_signals();`
- Line 369 (Function): `undefer_signals();`
- Line 380 (Function): `undefer_signals();`
- Line 416 (Function): `undefer_signals();`
- Line 435 (Function): `undefer_signals();`
- Line 445 (Function): `undefer_signals();`
- Line 478 (Function): `undefer_signals();`
- Line 488 (Function): `undefer_signals();`

#### MATH (36 occurrences)

**hoard2/processheap.cpp:**
- Line 49 (Function): `_log((Log<MemoryRequest>*)`

**openbsd/malloc.c:**
- Line 2558 (Function): `ulog(const char *format, ...)`
- Line 2679 (Function): `ulog("%18p %7zu %6u %6zu addr2line -e %s\n",`
- Line 2693 (Function): `ulog("%18p %7zu %6u %6zu addr2line -e %s\n",`
- Line 2697 (Function): `ulog("%*p %*s %6s %6s addr2line -e %s\n",`
- Line 2707 (Function): `ulog("Leak report:\n");`
- Line 2708 (Function): `ulog("                 f     sum      #    avg\n");`
- Line 2720 (Function): `ulog("chunk %18p %18p %4zu %d/%d\n",`
- Line 2733 (Function): `ulog("       ->");`
- Line 2743 (Function): `ulog("Free chunk structs:\n");`
- Line 2744 (Function): `ulog("Bkt) #CI                     page"`
- Line 2755 (Function): `ulog("%3d) %3d ", i, count);`
- Line 2757 (Function): `ulog("         ");`
- Line 2761 (Function): `ulog(".\n");`
- Line 2773 (Function): `ulog("Cached in small cache:\n");`
- Line 2777 (Function): `ulog("%zu(%u): %u = %zu\n", i + 1, cache->max,`
- Line 2782 (Function): `ulog("Cached in big cache: %zu/%zu\n", d->bigcache_used,`
- Line 2786 (Function): `ulog("%zu: %zu\n", i, d->bigcache[i].psize);`
- Line 2789 (Function): `ulog("Free pages cached: %zu\n", total);`
- Line 2798 (Function): `ulog("Malloc dir of %s pool %d at %p\n", __progname, poolno, d);`
- Line 2799 (Function): `ulog("MT=%d J=%d Fl=%#x\n", d->malloc_mt, d->malloc_junk,`
- Line 2801 (Function): `ulog("Region slots free %zu/%zu\n",`
- Line 2803 (Function): `ulog("Inserts %zu/%zu\n", d->inserts, d->insert_collisions);`
- Line 2804 (Function): `ulog("Deletes %zu/%zu\n", d->deletes, d->delete_moves);`
- Line 2805 (Function): `ulog("Cheap reallocs %zu/%zu\n",`
- Line 2807 (Function): `ulog("In use %zu\n", d->malloc_used);`
- Line 2808 (Function): `ulog("Guarded %zu\n", d->malloc_guarded);`
- Line 2811 (Function): `ulog("Hash table:\n");`
- Line 2812 (Function): `ulog("slot)  hash d  type               page                  "`
- Line 2820 (Function): `ulog("%4zx) #%4zx %zd ",`
- Line 2826 (Function): `ulog("pages %18p %18p %zu\n", d->r[i].p,`
- Line 2835 (Function): `ulog("\n");`
- Line 2873 (Function): `ulog("\n");`
- Line 2883 (Function): `ulog("******** Start dump %s *******\n", __progname);`
- Line 2884 (Function): `ulog("M=%u I=%d F=%d U=%d J=%d R=%d X=%d C=%#x cache=%u "`
- Line 2894 (Function): `ulog("******** End dump %s *******\n", __progname);`


### musl
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/musl`
**Files:** 383 sources, 27 headers

⚠️  **External dependencies detected:**

#### MATH (382 occurrences)

**complex/__cexp.c:**
- Line 40 (Function): `static double __frexp_exp(double x, int *expt)`
- Line 51 (Function): `exp_x = exp(x - kln2);`
- Line 67 (Function): `double complex __ldexp_cexp(double complex z, int expt)`
- Line 74 (Function): `exp_x = __frexp_exp(x, &ex_expt);`
- Line 86 (Function): `return CMPLX(cos(y) * exp_x * scale1 * scale2, sin(y) * exp_x * scale1 * scale2);`
- Line 86 (Function): `return CMPLX(cos(y) * exp_x * scale1 * scale2, sin(y) * exp_x * scale1 * scale2);`

**complex/cacosh.c:**
- Line 9 (Function): `z = cacos(z);`

**complex/catan.c:**
- Line 63 (Function): `double complex catan(double complex z)`
- Line 82 (Function): `w = CMPLX(w, 0.25 * log(a));`

**complex/catanh.c:**
- Line 7 (Function): `z = catan(CMPLX(-cimag(z), creal(z)));`

**complex/catanl.c:**
- Line 60 (Include): `#include <complex.h>`
- Line 67 (Function): `return catan(z);`

**complex/ccos.c:**
- Line 5 (Function): `double complex ccos(double complex z)`

**complex/ccosh.c:**
- Line 60 (Function): `return CMPLX(cosh(x) * cos(y), sinh(x) * sin(y));`
- Line 60 (Function): `return CMPLX(cosh(x) * cos(y), sinh(x) * sin(y));`
- Line 65 (Function): `h = exp(fabs(x)) * 0.5;`
- Line 66 (Function): `return CMPLX(h * cos(y), copysign(h, x) * sin(y));`
- Line 66 (Function): `return CMPLX(h * cos(y), copysign(h, x) * sin(y));`
- Line 69 (Function): `z = __ldexp_cexp(CMPLX(fabs(x), y), -1);`
- Line 74 (Function): `return CMPLX(h * h * cos(y), h * sin(y));`
- Line 74 (Function): `return CMPLX(h * h * cos(y), h * sin(y));`
- Line 125 (Function): `return CMPLX((x * x) * cos(y), x * sin(y));`
- Line 125 (Function): `return CMPLX((x * x) * cos(y), x * sin(y));`

**complex/ccosl.c:**
- Line 6 (Function): `return ccos(z);`

**complex/cexp.c:**
- Line 34 (Function): `double complex cexp(double complex z)`
- Line 47 (Function): `return CMPLX(exp(x), y);`
- Line 51 (Function): `return CMPLX(cos(y), sin(y));`
- Line 51 (Function): `return CMPLX(cos(y), sin(y));`
- Line 71 (Function): `return __ldexp_cexp(z, 0);`
- Line 80 (Function): `exp_x = exp(x);`
- Line 81 (Function): `return CMPLX(exp_x * cos(y), exp_x * sin(y));`
- Line 81 (Function): `return CMPLX(exp_x * cos(y), exp_x * sin(y));`

**complex/creal.c:**
- Line 1 (Include): `#include <complex.h>`

**complex/crealf.c:**
- Line 1 (Include): `#include <complex.h>`

**complex/creall.c:**
- Line 1 (Include): `#include <complex.h>`

**complex/csin.c:**
- Line 5 (Function): `double complex csin(double complex z)`

**complex/csinh.c:**
- Line 60 (Function): `return CMPLX(sinh(x) * cos(y), cosh(x) * sin(y));`
- Line 60 (Function): `return CMPLX(sinh(x) * cos(y), cosh(x) * sin(y));`
- Line 65 (Function): `h = exp(fabs(x)) * 0.5;`
- Line 66 (Function): `return CMPLX(copysign(h, x) * cos(y), h * sin(y));`
- Line 66 (Function): `return CMPLX(copysign(h, x) * cos(y), h * sin(y));`
- Line 69 (Function): `z = __ldexp_cexp(CMPLX(fabs(x), y), -1);`
- Line 74 (Function): `return CMPLX(h * cos(y), h * h * sin(y));`
- Line 74 (Function): `return CMPLX(h * cos(y), h * h * sin(y));`
- Line 126 (Function): `return CMPLX(x * cos(y), INFINITY * sin(y));`
- Line 126 (Function): `return CMPLX(x * cos(y), INFINITY * sin(y));`

**complex/csinl.c:**
- Line 6 (Function): `return csin(z);`

**complex/csqrt.c:**
- Line 42 (Function): `double complex __csqrt(double complex z)`
- Line 89 (Function): `t = sqrt((a + hypot(a, b)) * 0.5);`
- Line 92 (Function): `t = sqrt((-a + hypot(a, b)) * 0.5);`

**complex/csqrtf.c:**
- Line 76 (Function): `t = sqrt((a + hypot(a, b)) * 0.5);`
- Line 79 (Function): `t = sqrt((-a + hypot(a, b)) * 0.5);`

**complex/ctan.c:**
- Line 5 (Function): `double complex ctan(double complex z)`

**complex/ctanh.c:**
- Line 100 (Function): `return CMPLX(x, copysign(0, isinf(y) ? y : sin(y) * cos(y)));`
- Line 100 (Function): `return CMPLX(x, copysign(0, isinf(y) ? y : sin(y) * cos(y)));`
- Line 118 (Function): `double exp_mx = exp(-fabs(x));`
- Line 119 (Function): `return CMPLX(copysign(1, x), 4 * sin(y) * cos(y) * exp_mx * exp_mx);`
- Line 119 (Function): `return CMPLX(copysign(1, x), 4 * sin(y) * cos(y) * exp_mx * exp_mx);`
- Line 123 (Function): `t = tan(y);`

**complex/ctanl.c:**
- Line 6 (Function): `return ctan(z);`

**internal/complex_impl.h:**
- Line 4 (Include): `#include <complex.h>`
- Line 19 (Function): `hidden double complex __ldexp_cexp(double complex,int);`

**internal/libm.h:**
- Line 6 (Include): `#include <math.h>`
- Line 237 (Function): `hidden double __sin(double,double,int);`
- Line 238 (Function): `hidden double __cos(double,double);`
- Line 239 (Function): `hidden double __tan(double,double,int);`

**math/__cos.c:**
- Line 61 (Function): `double __cos(double x, double y)`

**math/__expo2.c:**
- Line 15 (Function): `return exp(x - kln2) * scale * scale;`

**math/__fpclassify.c:**
- Line 1 (Include): `#include <math.h>`

**math/__fpclassifyf.c:**
- Line 1 (Include): `#include <math.h>`

**math/__sin.c:**
- Line 12 (Function): `/* __sin( x, y, iy)`
- Line 52 (Function): `double __sin(double x, double y, int iy)`

**math/__tan.c:**
- Line 11 (Function): `/* __tan( x, y, k )`
- Line 66 (Function): `double __tan(double x, double y, int odd)`

**math/acos.c:**
- Line 12 (Function): `/* acos(x)`
- Line 60 (Function): `double acos(double x)`
- Line 89 (Function): `s = sqrt(z);`
- Line 95 (Function): `s = sqrt(z);`

**math/acosh.c:**
- Line 18 (Function): `return log1p(x-1 + sqrt((x-1)*(x-1)+2*(x-1)));`
- Line 21 (Function): `return log(2*x - 1/(x+sqrt(x*x-1)));`
- Line 21 (Function): `return log(2*x - 1/(x+sqrt(x*x-1)));`
- Line 23 (Function): `return log(x) + 0.693147180559945309417232121458176568;`

**math/acosl.c:**
- Line 22 (Function): `return acos(x);`

**math/arm/fabs.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm/fabsf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm/fma.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm/fmaf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm/sqrt.c:**
- Line 1 (Include): `#include <math.h>`
- Line 5 (Function): `double sqrt(double x)`

**math/arm/sqrtf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/ceil.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/ceilf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fabs.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fabsf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/floor.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/floorf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fma.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fmaf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fmax.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fmaxf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fmin.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/fminf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/llrint.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/llrintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/llround.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/llroundf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/lrint.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/lrintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/lround.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/lroundf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/nearbyint.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/nearbyintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/rint.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/rintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/round.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/roundf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/sqrt.c:**
- Line 1 (Include): `#include <math.h>`
- Line 3 (Function): `double sqrt(double x)`

**math/arm64/sqrtf.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/trunc.c:**
- Line 1 (Include): `#include <math.h>`

**math/arm64/truncf.c:**
- Line 1 (Include): `#include <math.h>`

**math/asin.c:**
- Line 12 (Function): `/* asin(x)`
- Line 67 (Function): `double asin(double x)`
- Line 92 (Function): `s = sqrt(z);`

**math/asinf.c:**
- Line 56 (Function): `s = sqrt(z);`

**math/asinh.c:**
- Line 16 (Function): `x = log(x) + 0.693147180559945309417232121458176568;`
- Line 19 (Function): `x = log(2*x + 1/(sqrt(x*x+1)+x));`
- Line 19 (Function): `x = log(2*x + 1/(sqrt(x*x+1)+x));`
- Line 22 (Function): `x = log1p(x + x*x/(sqrt(x*x+1)+1));`

**math/asinl.c:**
- Line 22 (Function): `return asin(x);`

**math/atan.c:**
- Line 12 (Function): `/* atan(x)`
- Line 63 (Function): `double atan(double x)`

**math/atan2.c:**
- Line 56 (Function): `return atan(y);`
- Line 99 (Function): `z = atan(fabs(y/x));`

**math/atanl.c:**
- Line 22 (Function): `return atan(x);`

**math/cbrt.c:**
- Line 18 (Include): `#include <math.h>`

**math/cbrtf.c:**
- Line 20 (Include): `#include <math.h>`

**math/copysignf.c:**
- Line 1 (Include): `#include <math.h>`

**math/cos.c:**
- Line 12 (Function): `/* cos(x)`
- Line 45 (Function): `double cos(double x)`
- Line 61 (Function): `return __cos(x, 0);`
- Line 71 (Function): `case 0: return  __cos(y[0], y[1]);`
- Line 72 (Function): `case 1: return -__sin(y[0], y[1], 1);`
- Line 73 (Function): `case 2: return -__cos(y[0], y[1]);`
- Line 75 (Function): `return  __sin(y[0], y[1], 1);`

**math/cosh.c:**
- Line 3 (Function): `/* cosh(x) = (exp(x) + 1/exp(x))/2`
- Line 31 (Function): `t = exp(x);`

**math/cosl.c:**
- Line 5 (Function): `return cos(x);`
- Line 18 (Define): `if (x < M_PI_4) {`

**math/erf.c:**
- Line 210 (Function): `return exp(-z*z-0.5625)*exp((z-x)*(z+x)+R/S)/x;`

**math/exp.c:**
- Line 8 (Include): `#include <math.h>`
- Line 72 (Function): `double exp(double x)`

**math/exp10.c:**
- Line 2 (Include): `#include <math.h>`
- Line 22 (Function): `return pow(10.0, x);`

**math/exp10f.c:**
- Line 2 (Include): `#include <math.h>`

**math/exp10l.c:**
- Line 3 (Include): `#include <math.h>`

**math/exp2.c:**
- Line 8 (Include): `#include <math.h>`

**math/exp2f.c:**
- Line 8 (Include): `#include <math.h>`

**math/expf.c:**
- Line 8 (Include): `#include <math.h>`

**math/expl.c:**
- Line 73 (Function): `return exp(x);`
- Line 126 (Function): `return exp(x);`

**math/expm1l.c:**
- Line 59 (Function): `/* exp(x) - 1 = x + 0.5 x^2 + x^3 P(x)/Q(x)`
- Line 110 (Function): `/* exp(x) = exp(k ln 2) exp(remainder ln 2) = 2^k exp(remainder ln 2).`
- Line 111 (Function): `We have qx = exp(remainder ln 2) - 1, so`
- Line 112 (Function): `exp(x) - 1  =  2^k (qx + 1) - 1  =  2^k qx + 2^k - 1.  */`

**math/fabs.c:**
- Line 1 (Include): `#include <math.h>`

**math/fabsf.c:**
- Line 1 (Include): `#include <math.h>`

**math/fdim.c:**
- Line 1 (Include): `#include <math.h>`

**math/fdimf.c:**
- Line 1 (Include): `#include <math.h>`

**math/fdiml.c:**
- Line 1 (Include): `#include <math.h>`

**math/finite.c:**
- Line 2 (Include): `#include <math.h>`

**math/finitef.c:**
- Line 2 (Include): `#include <math.h>`

**math/fma.c:**
- Line 3 (Include): `#include <math.h>`

**math/fmaf.c:**
- Line 28 (Include): `#include <fenv.h>`
- Line 29 (Include): `#include <math.h>`

**math/fmal.c:**
- Line 36 (Include): `#include <fenv.h>`

**math/fmax.c:**
- Line 1 (Include): `#include <math.h>`

**math/fmaxf.c:**
- Line 1 (Include): `#include <math.h>`

**math/fmaxl.c:**
- Line 1 (Include): `#include <math.h>`

**math/fmin.c:**
- Line 1 (Include): `#include <math.h>`

**math/fminf.c:**
- Line 1 (Include): `#include <math.h>`

**math/fminl.c:**
- Line 1 (Include): `#include <math.h>`

**math/fmod.c:**
- Line 1 (Include): `#include <math.h>`

**math/fmodf.c:**
- Line 1 (Include): `#include <math.h>`

**math/frexp.c:**
- Line 1 (Include): `#include <math.h>`
- Line 4 (Function): `double frexp(double x, int *e)`
- Line 11 (Function): `x = frexp(x*0x1p64, e);`

**math/frexpf.c:**
- Line 1 (Include): `#include <math.h>`

**math/frexpl.c:**
- Line 6 (Function): `return frexp(x, e);`

**math/hypot.c:**
- Line 1 (Include): `#include <math.h>`
- Line 66 (Function): `return z*sqrt(ly+lx+hy+hx);`

**math/hypotf.c:**
- Line 1 (Include): `#include <math.h>`

**math/j0.c:**
- Line 78 (Function): `s = sin(x);`
- Line 79 (Function): `c = cos(x);`
- Line 86 (Function): `z = -cos(2*x);`
- Line 97 (Function): `return invsqrtpi*cc/sqrt(x);`
- Line 185 (Function): `return u/v + tpi*(j0(x)*log(x));`
- Line 187 (Function): `return u00 + tpi*log(x);`

**math/j1.c:**
- Line 77 (Function): `s = sin(x);`
- Line 80 (Function): `c = cos(x);`
- Line 85 (Function): `z = cos(2*x);`
- Line 98 (Function): `return invsqrtpi*cc/sqrt(x);`
- Line 173 (Function): `return x*(u/v) + tpi*(j1(x)*log(x)-1/x);`

**math/jn.c:**
- Line 90 (Function): `case 0: temp = -cos(x)+sin(x); break;`
- Line 90 (Function): `case 0: temp = -cos(x)+sin(x); break;`
- Line 91 (Function): `case 1: temp = -cos(x)-sin(x); break;`
- Line 91 (Function): `case 1: temp = -cos(x)-sin(x); break;`
- Line 92 (Function): `case 2: temp =  cos(x)-sin(x); break;`
- Line 92 (Function): `case 2: temp =  cos(x)-sin(x); break;`
- Line 94 (Function): `case 3: temp =  cos(x)+sin(x); break;`
- Line 94 (Function): `case 3: temp =  cos(x)+sin(x); break;`
- Line 96 (Function): `b = invsqrtpi*temp/sqrt(x);`
- Line 175 (Function): `/*  estimate log((2/x)^n*n!) = n*log(2/x)+n*ln(n)`
- Line 183 (Function): `tmp = nf*log(fabs(w));`
- Line 259 (Function): `case 0: temp = -sin(x)-cos(x); break;`
- Line 259 (Function): `case 0: temp = -sin(x)-cos(x); break;`
- Line 260 (Function): `case 1: temp = -sin(x)+cos(x); break;`
- Line 260 (Function): `case 1: temp = -sin(x)+cos(x); break;`
- Line 261 (Function): `case 2: temp =  sin(x)+cos(x); break;`
- Line 261 (Function): `case 2: temp =  sin(x)+cos(x); break;`
- Line 263 (Function): `case 3: temp =  sin(x)-cos(x); break;`
- Line 263 (Function): `case 3: temp =  sin(x)-cos(x); break;`
- Line 265 (Function): `b = invsqrtpi*temp/sqrt(x);`

**math/jnf.c:**
- Line 123 (Function): `/*  estimate log((2/x)^n*n!) = n*log(2/x)+n*ln(n)`

**math/ldexp.c:**
- Line 1 (Include): `#include <math.h>`
- Line 3 (Function): `double ldexp(double x, int n)`

**math/ldexpf.c:**
- Line 1 (Include): `#include <math.h>`

**math/ldexpl.c:**
- Line 1 (Include): `#include <math.h>`

**math/lgamma.c:**
- Line 1 (Include): `#include <math.h>`

**math/lgamma_r.c:**
- Line 163 (Function): `case 0: return __sin(x, 0.0, 0);`
- Line 164 (Function): `case 1: return __cos(x, 0.0);`
- Line 165 (Function): `case 2: return __sin(-x, 0.0, 0);`
- Line 166 (Function): `case 3: return -__cos(x, 0.0);`
- Line 188 (Function): `return -log(x);`
- Line 199 (Function): `nadj = log(pi/(t*x));`
- Line 208 (Function): `r = -log(x);`
- Line 267 (Function): `r += log(z);`
- Line 271 (Function): `t = log(x);`
- Line 277 (Function): `r =  x*(log(x)-1.0);`

**math/lgammaf.c:**
- Line 1 (Include): `#include <math.h>`

**math/lgammal.c:**
- Line 187 (Function): `/* lgam(x) = ( x - 0.5 ) * log(x) - x + LS2PI + 1/x w(1/x^2)`

**math/llrint.c:**
- Line 1 (Include): `#include <math.h>`

**math/llrintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/llrintl.c:**
- Line 2 (Include): `#include <fenv.h>`

**math/llround.c:**
- Line 1 (Include): `#include <math.h>`

**math/llroundf.c:**
- Line 1 (Include): `#include <math.h>`

**math/llroundl.c:**
- Line 1 (Include): `#include <math.h>`

**math/log.c:**
- Line 8 (Include): `#include <math.h>`
- Line 28 (Function): `double log(double x)`

**math/log10.c:**
- Line 20 (Include): `#include <math.h>`

**math/log10f.c:**
- Line 16 (Include): `#include <math.h>`

**math/log10l.c:**
- Line 68 (Function): `/* Coefficients for log(1+x) = x - x**2/2 + x**3 P(x)/Q(x)`
- Line 92 (Function): `/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),`
- Line 138 (Function): `/* logarithm using log(x) = z + z**3 P(z)/Q(z),`

**math/log1pl.c:**
- Line 59 (Function): `/* Coefficients for log(1+x) = x - x^2 / 2 + x^3 P(x)/Q(x)`
- Line 82 (Function): `/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),`
- Line 129 (Function): `/* logarithm using log(x) = z + z^3 P(z)/Q(z),`

**math/log2.c:**
- Line 8 (Include): `#include <math.h>`

**math/log2f.c:**
- Line 8 (Include): `#include <math.h>`

**math/log2l.c:**
- Line 87 (Function): `/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),`
- Line 130 (Function): `/* logarithm using log(x) = z + z**3 P(z)/Q(z),`

**math/log_data.c:**
- Line 43 (Function): `log(x) = k ln2 + log(c) + log(z/c)`
- Line 44 (Function): `log(z/c) = poly(z/c - 1)`
- Line 50 (Function): `tab[i].logc = (double)log(c)`
- Line 59 (Function): `3) the rounding error in (double)log(c) is minimized (< 0x1p-66).`
- Line 65 (Function): `|log(x)| < 0x1p-4, this is not enough so that is special cased.  */`

**math/logb.c:**
- Line 1 (Include): `#include <math.h>`

**math/logbf.c:**
- Line 1 (Include): `#include <math.h>`

**math/logbl.c:**
- Line 1 (Include): `#include <math.h>`

**math/logf.c:**
- Line 8 (Include): `#include <math.h>`

**math/logl.c:**
- Line 60 (Function): `return log(x);`
- Line 63 (Function): `/* Coefficients for log(1+x) = x - x**2/2 + x**3 P(x)/Q(x)`
- Line 86 (Function): `/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),`
- Line 129 (Function): `/* logarithm using log(x) = z + z**3 P(z)/Q(z),`
- Line 173 (Function): `return log(x);`

**math/lrint.c:**
- Line 2 (Include): `#include <fenv.h>`
- Line 3 (Include): `#include <math.h>`

**math/lrintf.c:**
- Line 1 (Include): `#include <math.h>`

**math/lrintl.c:**
- Line 2 (Include): `#include <fenv.h>`

**math/lround.c:**
- Line 1 (Include): `#include <math.h>`

**math/lroundf.c:**
- Line 1 (Include): `#include <math.h>`

**math/lroundl.c:**
- Line 1 (Include): `#include <math.h>`

**math/nan.c:**
- Line 1 (Include): `#include <math.h>`

**math/nanf.c:**
- Line 1 (Include): `#include <math.h>`

**math/nanl.c:**
- Line 1 (Include): `#include <math.h>`

**math/nearbyint.c:**
- Line 1 (Include): `#include <fenv.h>`
- Line 2 (Include): `#include <math.h>`

**math/nearbyintf.c:**
- Line 1 (Include): `#include <fenv.h>`
- Line 2 (Include): `#include <math.h>`

**math/nearbyintl.c:**
- Line 1 (Include): `#include <math.h>`
- Line 10 (Include): `#include <fenv.h>`

**math/nexttowardl.c:**
- Line 1 (Include): `#include <math.h>`

**math/pow.c:**
- Line 8 (Include): `#include <math.h>`
- Line 33 (Function): `/* Compute y+TAIL = log(x) where the rounded result is y and TAIL has about`
- Line 164 (Function): `/* Computes sign*exp(x+xtail) where |xtail| < 2^-8/N and |xtail| <= |x|.`
- Line 255 (Function): `double pow(double x, double y)`
- Line 267 (Function): `/* Note: if |y| > 1075 * ln2 * 2^53 ~= 0x1.749p62 then pow(x,y) = inf/0`
- Line 268 (Function): `and if |y| < 2^-54 / 1075 ~= 0x1.e7b6p-65 then pow(x,y) = +-1.  */`

**math/pow_data.c:**
- Line 30 (Function): `log(x) = k ln2 + log(c) + log(z/c)`
- Line 31 (Function): `log(z/c) = poly(z/c - 1)`
- Line 37 (Function): `tab[i].logc = round(0x1p43*log(c))/0x1p43`
- Line 38 (Function): `tab[i].logctail = (double)(log(c) - logc)`
- Line 45 (Function): `Note: |z/c - 1| < 1/N for the chosen c, |log(c) - logc - logctail| < 0x1p-97,`
- Line 47 (Function): `error and the interval for z is selected such that near x == 1, where log(x)`

**math/powf.c:**
- Line 6 (Include): `#include <math.h>`

**math/powl.c:**
- Line 75 (Function): `return pow(x, y);`
- Line 82 (Function): `/* log(1+x) =  x - .5x^2 + x^3 *  P(z)/Q(z)`
- Line 324 (Function): `/* rational approximation for log(1+v):`
- Line 520 (Function): `return pow(x, y);`

**math/remainder.c:**
- Line 1 (Include): `#include <math.h>`

**math/remainderf.c:**
- Line 1 (Include): `#include <math.h>`

**math/remainderl.c:**
- Line 1 (Include): `#include <math.h>`

**math/remquo.c:**
- Line 1 (Include): `#include <math.h>`

**math/remquof.c:**
- Line 1 (Include): `#include <math.h>`

**math/rint.c:**
- Line 2 (Include): `#include <math.h>`

**math/rintf.c:**
- Line 2 (Include): `#include <math.h>`

**math/riscv64/copysign.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/copysignf.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fabs.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fabsf.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fma.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fmaf.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fmax.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fmaxf.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fmin.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/fminf.c:**
- Line 1 (Include): `#include <math.h>`

**math/riscv64/sqrt.c:**
- Line 1 (Include): `#include <math.h>`
- Line 5 (Function): `double sqrt(double x)`

**math/riscv64/sqrtf.c:**
- Line 1 (Include): `#include <math.h>`

**math/scalb.c:**
- Line 19 (Include): `#include <math.h>`

**math/scalbf.c:**
- Line 17 (Include): `#include <math.h>`

**math/scalbln.c:**
- Line 2 (Include): `#include <math.h>`

**math/scalblnf.c:**
- Line 2 (Include): `#include <math.h>`

**math/scalblnl.c:**
- Line 2 (Include): `#include <math.h>`

**math/scalbn.c:**
- Line 1 (Include): `#include <math.h>`

**math/scalbnf.c:**
- Line 1 (Include): `#include <math.h>`

**math/signgam.c:**
- Line 1 (Include): `#include <math.h>`

**math/significand.c:**
- Line 2 (Include): `#include <math.h>`

**math/significandf.c:**
- Line 2 (Include): `#include <math.h>`

**math/sin.c:**
- Line 12 (Function): `/* sin(x)`
- Line 45 (Function): `double sin(double x)`
- Line 62 (Function): `return __sin(x, 0.0, 0);`
- Line 72 (Function): `case 0: return  __sin(y[0], y[1], 1);`
- Line 73 (Function): `case 1: return  __cos(y[0], y[1]);`
- Line 74 (Function): `case 2: return -__sin(y[0], y[1], 1);`
- Line 76 (Function): `return -__cos(y[0], y[1]);`

**math/sincos.c:**
- Line 16 (Function): `void sincos(double x, double *sin, double *cos)`
- Line 48 (Function): `s = __sin(y[0], y[1], 1);`
- Line 49 (Function): `c = __cos(y[0], y[1]);`

**math/sincosl.c:**
- Line 8 (Function): `sincos(x, &sind, &cosd);`
- Line 24 (Define): `if (u.f < M_PI_4) {`

**math/sinh.c:**
- Line 3 (Function): `/* sinh(x) = (exp(x) - 1/exp(x))/2`

**math/sinl.c:**
- Line 6 (Function): `return sin(x);`
- Line 18 (Define): `if (u.f < M_PI_4) {`

**math/sqrt.c:**
- Line 12 (Function): `/* sqrt(x)`
- Line 83 (Function): `double sqrt(double x)`

**math/sqrtl.c:**
- Line 1 (Include): `#include <math.h>`
- Line 6 (Function): `return sqrt(x);`

**math/tan.c:**
- Line 12 (Function): `/* tan(x)`
- Line 44 (Function): `double tan(double x)`
- Line 60 (Function): `return __tan(x, 0.0, 0);`
- Line 69 (Function): `return __tan(y[0], y[1], n&1);`

**math/tanh.c:**
- Line 3 (Function): `/* tanh(x) = (exp(x) - exp(-x))/(exp(x) + exp(-x))`

**math/tanl.c:**
- Line 6 (Function): `return tan(x);`
- Line 18 (Define): `if (u.f < M_PI_4) {`

**math/tgamma.c:**
- Line 10 (Function): `exp(x + g - 0.5)`
- Line 21 (Function): `Gamma(x)*Gamma(-x) = -pi/(x sin(pi x))`
- Line 48 (Function): `return __sin(x, 0, 0);`
- Line 50 (Function): `return __cos(x, 0);`
- Line 52 (Function): `return __sin(-x, 0, 0);`
- Line 54 (Function): `return -__cos(x, 0);`
- Line 159 (Function): `r = S(absx) * exp(-y);`
- Line 168 (Function): `z = pow(y, 0.5*z);`
- Line 199 (Function): `return -log(absx);`
- Line 206 (Function): `return log(fabs(x));`
- Line 211 (Function): `r = (absx-0.5)*(log(absx+gmhalf)-1) + (log(S(absx)) - (gmhalf+0.5));`
- Line 216 (Function): `r = log(pi/(fabs(x)*absx)) - r;`

**math/tgammaf.c:**
- Line 1 (Include): `#include <math.h>`

**math/tgammal.c:**
- Line 116 (Function): `tgamma(x) = sqrt(2 pi) x^(x-.5) exp(-x) (1 + 1/x P(1/x))`
- Line 116 (Function): `tgamma(x) = sqrt(2 pi) x^(x-.5) exp(-x) (1 + 1/x P(1/x))`

**math/x86_64/fma.c:**
- Line 1 (Include): `#include <math.h>`

**math/x86_64/fmaf.c:**
- Line 1 (Include): `#include <math.h>`


### pthread
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/pthread`
**Files:** 15 sources, 0 headers

✅ **No external dependencies found**

### signal
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/signal`
**Files:** 26 sources, 0 headers

✅ **No external dependencies found**

### stdio
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/stdio`
**Files:** 6 sources, 0 headers

✅ **No external dependencies found**

### stdlib
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/stdlib`
**Files:** 19 sources, 0 headers

⚠️  **External dependencies detected:**

#### FREETYPE (2 occurrences)

**strfmon.c:**
- Line 199 (Define): `flags |= LEFT_JUSTIFY;`
- Line 387 (Define): `if (flags & LEFT_JUSTIFY) {`


### string
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/string`
**Files:** 26 sources, 0 headers

✅ **No external dependencies found**

### sys
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/sys`
**Files:** 25 sources, 0 headers

✅ **No external dependencies found**

### time
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/time`
**Files:** 11 sources, 0 headers

⚠️  **External dependencies detected:**

#### ICU (1 occurrences)

**timer_support.cpp:**
- Line 69 (Function): `undefer_signals();`


### unistd
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/unistd`
**Files:** 37 sources, 0 headers

⚠️  **External dependencies detected:**

#### ICU (4 occurrences)

**fork.c:**
- Line 115 (Function): `undefer_signals();`
- Line 130 (Function): `undefer_signals();`
- Line 146 (Function): `undefer_signals();`
- Line 173 (Function): `undefer_signals();`


### wchar
**Path:** `/home/ruslan/haiku/src/system/libroot/posix/wchar`
**Files:** 48 sources, 0 headers

✅ **No external dependencies found**

## Recommendations

Based on the analysis, the following BuildFeatures integration is recommended:

- **crypt**: math
- **glibc**: math, icu
- **malloc**: math, icu
- **musl**: math
- **stdlib**: freetype
- **time**: icu
- **unistd**: icu

Update `libroot_posix_modules_generator.py` module feature map:

```python
module_feature_map = {
    'glibc': ['icu'],
    'malloc': ['icu'],
    'stdlib': ['freetype'],
    'time': ['icu'],
    'unistd': ['icu'],
}
```

---
*Generated by posix_dependencies_analyzer.py on 2025-08-25 15:58:15*