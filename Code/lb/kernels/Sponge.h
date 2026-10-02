// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_LB_KERNELS_SPONGE_H
#define HEMELB_LB_KERNELS_SPONGE_H
#include <cmath>
#include "geometry/Domain.h"
#include "lb/HydroVars.h"
#include "lb/SimulationState.h"

namespace hemelb::lb
{
inline double SpongeTau(double baseTau, double ratio, double distance, double width,
                        LatticeTimeStep time, LatticeTimeStep lifetime)
{
    if (width <= 0 || distance > width || time >= lifetime)
        return baseTau;
    double profile = (ratio - 1) * std::pow(1 - distance / width, 2);
    double decay = time <= lifetime / 2 ? 1 : 2 * (1 - double(time) / lifetime);
    return 0.5 + (baseTau - 0.5) * (1 + profile * decay);
}
inline double SmagorinskyTau(double baseTau, double density, double stressNorm, double coefficient)
{
    return 0.5 * (baseTau + std::sqrt(baseTau * baseTau + 18 * std::sqrt(2.0) * coefficient *
                                                              coefficient * stressNorm / density));
}
template <lattice_type L, bool TwoRelaxation = false, bool LES = false> class SpongeKernel
{
    LbmParameters const *params;
    SimulationState const *state;
    std::vector<double> distances;

  public:
    using LatticeType = L;
    using VarsType = HydroVars<SpongeKernel>;
    explicit SpongeKernel(InitParams &init) : params(init.lbmParams), state(init.state)
    {
        if (!params || params->spongeWidth <= 0 || params->spongeLifetime == 0)
            throw Exception() << "Sponge kernels require initialconditions/sponge_layer";
        if (!init.latDat)
            throw Exception() << "Sponge kernels require geometry";
        distances.resize(init.latDat->GetLocalFluidSiteCount(),
                         std::numeric_limits<double>::infinity());
        for (site_t i = 0; i < site_t(distances.size()); ++i)
            for (auto const &outlet : params->outletPositions)
                distances[i] = std::min(
                    distances[i],
                    (init.latDat->GetSite(i).GetGlobalSiteCoords().template as<double>() - outlet)
                        .GetMagnitude());
    }
    void CalculateDensityMomentumFeq(VarsType &vars, site_t index)
    {
        L::CalculateDensityMomentumFEq(vars.f, vars.density, vars.momentum, vars.velocity,
                                       vars.GetFEq());
        Finish(vars, index);
    }
    void CalculateFeq(VarsType &vars, site_t index)
    {
        L::CalculateFeq(vars.density, vars.momentum, vars.GetFEq());
        Finish(vars, index);
    }
    void Finish(VarsType &vars, site_t index)
    {
        for (Direction d = 0; d < L::NUMVECTORS; ++d)
            vars.SetFNeq(d, vars.f[d] - vars.GetFEq()[d]);
        // Ghost evaluations may pass index zero, retaining the source kernel's convention.
        vars.tau = SpongeTau(params->GetTau(), params->spongeRatio, distances.at(index),
                             params->spongeWidth, state ? state->GetTimeStep() : 0,
                             params->spongeLifetime);
        if constexpr (LES)
        {
            double norm2 = 0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                {
                    double stress = 0;
                    for (Direction d = 0; d < L::NUMVECTORS; ++d)
                        stress += L::VECTORS[d][a] * L::VECTORS[d][b] * vars.GetFNeq()[d];
                    norm2 += stress * stress;
                }
            if (vars.density <= 0)
                throw Exception() << "LES requires positive density";
            vars.tau =
                SmagorinskyTau(vars.tau, vars.density, std::sqrt(norm2), params->smagorinsky);
        }
    }
    void Collide(LbmParameters const *, VarsType &vars)
    {
        double even = -1 / vars.tau;
        double odd = -1 / (0.5 + (3.0 / 16) / (vars.tau - 0.5));
        for (Direction d = 0; d < L::NUMVECTORS; ++d)
        {
            double correction = even * vars.GetFNeq()[d];
            if constexpr (TwoRelaxation)
            {
                auto inverse = L::INVERSEDIRECTIONS[d];
                correction = 0.5 * (even * (vars.GetFNeq()[d] + vars.GetFNeq()[inverse]) +
                                    odd * (vars.GetFNeq()[d] - vars.GetFNeq()[inverse]));
            }
            vars.SetFPostCollision(d, vars.f[d] + correction);
        }
    }
};
template <lattice_type L> using LBGKSL = SpongeKernel<L>;
template <lattice_type L> using TRTSL = SpongeKernel<L, true>;
template <lattice_type L> using LBGKLESSL = SpongeKernel<L, false, true>;
} // namespace hemelb::lb
#endif
