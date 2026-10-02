// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "util/Bessel.h"

#include <cmath>
#include "hassert.h"

namespace hemelb::util
{
    std::complex<double> BesselJ1ComplexArgument(std::complex<double> const &z, double tolSq)
    {
        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || !std::isfinite(tolSq) ||
            tolSq <= 0)
            throw Exception() << "Bessel J1 requires finite argument and positive tolerance";
        auto term = 0.5 * z;
        auto sum = term;
        for (unsigned k = 1; k < 10000; ++k)
        {
            term *= -0.25 * z * z / (double(k) * (k + 1));
            sum += term;
            if (!std::isfinite(sum.real()) || !std::isfinite(sum.imag()))
                throw Exception() << "Bessel J1 series overflow";
            if (std::norm(term) <= tolSq)
                return sum;
        }
        throw Exception() << "Bessel J1 series did not converge";
    }

    std::complex<double> BesselJ0ComplexArgument(const std::complex<double> &z, double tolSq)
    {
        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || !std::isfinite(tolSq) ||
            tolSq <= 0)
            throw Exception() << "Bessel J0 requires finite argument and positive tolerance";
        std::complex<double> sum{1, 0}, term{1, 0};
        for (unsigned k = 1; k < 10000; ++k)
        {
            term *= -0.25 * z * z / (double(k) * k);
            sum += term;
            if (!std::isfinite(sum.real()) || !std::isfinite(sum.imag()))
                throw Exception() << "Bessel J0 series overflow";
            if (std::norm(term) <= tolSq)
                return sum;
        }
        throw Exception() << "Bessel J0 series did not converge";
    }
}
