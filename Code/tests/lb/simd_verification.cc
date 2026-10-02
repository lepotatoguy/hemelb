// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.
#include <cmath>
#include <iostream>
#include "lb/lattices/D3Q15.h"
#include "lb/lattices/D3Q19.h"
#include "lb/lattices/D3Q27.h"
#include "lb/lattices/D3Q15i.h"

template <class L> bool Verify()
{
    for (unsigned n = 0; n < 1000; ++n)
    {
        std::array<double, L::NUMVECTORS> f{}, actual{};
        double expectedDensity = 0, density;
        hemelb::LatticeMomentum expectedMomentum{}, momentum;
        for (unsigned i = 0; i < L::NUMVECTORS; ++i)
        {
            f[i] = 0.2 + std::sin(0.123 * (n + 1) * (i + 1)) * 0.1;
            expectedDensity += f[i];
            expectedMomentum += L::VECTORS[i] * f[i];
        }
        L::CalculateDensityAndMomentum(f, density, momentum);
        if (std::abs(density - expectedDensity) > 1e-12 ||
            (momentum - expectedMomentum).GetMagnitude() > 1e-12)
            return false;
        L::CalculateFeq(density, momentum, actual);
        double inverse = L::IsLatticeCompressible() ? 1 / density : 1;
        for (unsigned i = 0; i < L::NUMVECTORS; ++i)
        {
            double dot = Dot(L::VECTORS[i], momentum);
            double expected =
                L::EQMWEIGHTS[i] * (density - 1.5 * inverse * momentum.GetMagnitudeSquared() +
                                    4.5 * inverse * dot * dot + 3 * dot);
            if (std::abs(actual[i] - expected) > 1e-12)
                return false;
        }
    }
    return true;
}
__attribute__((noinline)) bool VerifyAll()
{
    return Verify<hemelb::lb::D3Q15>() && Verify<hemelb::lb::D3Q19>() &&
           Verify<hemelb::lb::D3Q27>() && Verify<hemelb::lb::D3Q15i>();
}
#if defined(__x86_64__)
__attribute__((target("no-avx,no-avx2,no-avx512f")))
#endif
int main()
{
#if defined(HEMELB_USE_AVX512)
    if (!__builtin_cpu_supports("avx512f"))
    {
        std::cout << "SKIP: AVX512F unavailable\n";
        return 77;
    }
#elif defined(HEMELB_USE_AVX2)
    if (!__builtin_cpu_supports("avx2"))
    {
        std::cout << "SKIP: AVX2 unavailable\n";
        return 77;
    }
#endif
    if (!VerifyAll())
        return 1;
    std::cout << "Density, momentum and equilibrium checks passed for D3Q15/19/27/15i\n";
}
