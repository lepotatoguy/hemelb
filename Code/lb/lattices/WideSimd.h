// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_LB_LATTICES_WIDESIMD_H
#define HEMELB_LB_LATTICES_WIDESIMD_H
#include <immintrin.h>
namespace hemelb::lb::detail
{
#if defined(HEMELB_USE_AVX512)
struct WideSimd
{
    using Vec = __m512d;
    static constexpr unsigned lanes = 8;
    static Vec zero() { return _mm512_setzero_pd(); }
    static Vec splat(double v) { return _mm512_set1_pd(v); }
    static Vec load(double const *v) { return _mm512_loadu_pd(v); }
    static void store(double *p, Vec v) { _mm512_storeu_pd(p, v); }
    static Vec add(Vec a, Vec b) { return _mm512_add_pd(a, b); }
    static Vec mul(Vec a, Vec b) { return _mm512_mul_pd(a, b); }
};
#else
struct WideSimd
{
    using Vec = __m256d;
    static constexpr unsigned lanes = 4;
    static Vec zero() { return _mm256_setzero_pd(); }
    static Vec splat(double v) { return _mm256_set1_pd(v); }
    static Vec load(double const *v) { return _mm256_loadu_pd(v); }
    static void store(double *p, Vec v) { _mm256_storeu_pd(p, v); }
    static Vec add(Vec a, Vec b) { return _mm256_add_pd(a, b); }
    static Vec mul(Vec a, Vec b) { return _mm256_mul_pd(a, b); }
};
#endif
inline double Sum(typename WideSimd::Vec v)
{
    double x[WideSimd::lanes];
    WideSimd::store(x, v);
    double sum = 0;
    for (double value : x)
        sum += value;
    return sum;
}
} // namespace hemelb::lb::detail
#endif
