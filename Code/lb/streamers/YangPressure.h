// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

// Parts adapted from HemePure (0bf67b16), Copyright (c) 2024,
// Centre for Computational Science (UCL). See COPYING.HemePure.
#ifndef HEMELB_LB_STREAMERS_YANGPRESSURE_H
#define HEMELB_LB_STREAMERS_YANGPRESSURE_H
#include <map>
#include <numeric>
#include "lb/streamers/Common.h"
#include "lb/iolets/BoundaryValues.h"
#include "geometry/neighbouring/NeighbouringDataManager.h"

namespace hemelb::lb
{
// Straight-iolet, single-relaxation equations from HemePure at 0bf67b16.
// Stencils are fixed at construction; zero and tangential vectors are excluded.
template <collision_type C> class YangPressureLink
{
  public:
    using CollisionType = C;
    using LatticeType = typename C::LatticeType;
    using VarsType = typename C::VarsType;

  private:
    C &collider;
    BoundaryValues &iolet;
    struct Stencil
    {
        LatticeVector first, second, cp;
        int width;
        Direction dirG, opposite;
    };
    std::map<std::pair<site_t, Direction>, Stencil> stencils;
    static auto Directions(LatticePosition const &normal)
    {
        std::vector<Direction> dirs;
        for (Direction d = 0; d < LatticeType::NUMVECTORS; ++d)
            if (Dot(LatticeType::VECTORS[d].template as<double>(), normal) > 1e-12)
                dirs.push_back(d);
        std::stable_sort(
            dirs.begin(), dirs.end(),
            [&](Direction a, Direction b)
            {
                return Dot(LatticeType::VECTORS[a].template as<double>().GetNormalised(), normal) >
                       Dot(LatticeType::VECTORS[b].template as<double>().GetNormalised(), normal);
            });
        return dirs;
    }

  public:
    YangPressureLink(C &c, InitParams &init) : collider(c), iolet(*init.boundaryObject)
    {
        for (auto const &[begin, end] : init.siteRanges)
            for (site_t i = begin; i < end; ++i)
            {
                auto site = init.latDat->GetSite(i);
                auto dirs = Directions(iolet.GetGlobalIolet(site.GetIoletId())->GetNormal());
                if (dirs.empty())
                    throw Exception() << "Yang iolet has no inward lattice direction";
                for (Direction d = 0; d < LatticeType::NUMVECTORS; ++d)
                    if (site.HasIolet(d))
                    {
                        auto outer = site.GetGlobalSiteCoords() + LatticeType::VECTORS[d];
                        bool found = false;
                        for (int width = 0; width < 3 && !found; ++width)
                            for (auto candidate : dirs)
                            {
                                auto cp = LatticeType::VECTORS[candidate].template as<site_t>();
                                auto first = outer + cp * (width + 1), second = first + cp;
                                if (!init.latDat->IsValidLatticeSite(first) ||
                                    !init.latDat->IsValidLatticeSite(second))
                                    continue;
                                auto p1 = init.latDat->GetProcIdFromGlobalCoords(first);
                                auto p2 = init.latDat->GetProcIdFromGlobalCoords(second);
                                if (p1 == SITE_OR_BLOCK_SOLID || p2 == SITE_OR_BLOCK_SOLID)
                                    continue;
                                stencils[{i, d}] = {
                                    first, second,       cp,
                                    width, dirs.front(), LatticeType::INVERSEDIRECTIONS[candidate]};
                                for (auto const &location : {first, second})
                                    if (init.latDat->GetProcIdFromGlobalCoords(location) !=
                                        init.latDat->GetLocalRank())
                                        init.neighbouringDataManager->RegisterNeededSite(
                                            init.latDat
                                                ->GetGlobalNoncontiguousSiteIdFromGlobalCoords(
                                                    location));
                                found = true;
                                break;
                            }
                        if (!found)
                            throw Exception()
                                << "Yang pressure requires two fluid sites behind iolet at "
                                << site.GetGlobalSiteCoords();
                    }
            }
    }
    void StreamLink(LbmParameters const *lbmParams, geometry::FieldData &data,
                    geometry::Site<geometry::FieldData> const &site, VarsType &hydroVars,
                    Direction direction)
    {
        auto const &s = stencils.at({site.GetIndex(), direction});
        auto const &domain = data.GetDomain();
        auto distributions = [&](LatticeVector const &location) -> distribn_t const *
        {
            if (domain.GetProcIdFromGlobalCoords(location) == domain.GetLocalRank())
                return data.GetSite(location).template GetFOld<LatticeType>().data();
            return data.GetNeighbouringData()
                .GetSite(domain.GetGlobalNoncontiguousSiteIdFromGlobalCoords(location))
                .template GetFOld<LatticeType>()
                .data();
        };
        distribn_t const *firstFluidFOld = distributions(s.first);
        distribn_t const *secondFluidFOld = distributions(s.second);
        double cut;
        if (domain.GetProcIdFromGlobalCoords(s.first) == domain.GetLocalRank())
            cut = data.GetSite(s.first).template GetWallDistance<LatticeType>(s.opposite);
        else
            cut = data.GetNeighbouringData()
                      .GetSite(domain.GetGlobalNoncontiguousSiteIdFromGlobalCoords(s.first))
                      .template GetWallDistance<LatticeType>(s.opposite);
        auto *localIOlet = iolet.GetGlobalIolet(site.GetIoletId());
        auto ioletNormal = localIOlet->GetNormal();
        auto dirG = s.dirG;
        auto unstreamed = LatticeType::INVERSEDIRECTIONS[direction];
        auto cp = s.cp;
        auto width = s.width;
        auto wallDistance = width + cut;
        // Calculate the densities, momenta, and equilibrium distributions and store them in
        // HydroVars.
        VarsType hVfirstFluid(firstFluidFOld);
        collider.kernel.CalculateDensityMomentumFeq(hVfirstFluid,
                                                    0); // the second argument is dummy
        VarsType hVsecondFluid(secondFluidFOld);
        collider.kernel.CalculateDensityMomentumFeq(hVsecondFluid,
                                                    0); // the second argument is dummy

        // Calculate the non-equilibrium distributions on the wall (equation 16).
        FVector<LatticeType> fNeqWall{};
        for (Direction i = 0; i < LatticeType::NUMVECTORS; ++i)
        {
            fNeqWall[i] = (1.0 + wallDistance) * hVfirstFluid.GetFNeq()[i] -
                          wallDistance * hVsecondFluid.GetFNeq()[i];
        }

        LatticeVector cg =
            LatticeVector(LatticeType::CX[dirG], LatticeType::CY[dirG], LatticeType::CZ[dirG]);
        distribn_t fNew;

        if (unstreamed == dirG && cg == cp)
        {
            // Calculate the density on the wall (equation 13).
            // The equilibrium density, 1, is absorbed in the iolet pressure.
            // Also note that h = 1 in lattice units.
            const distribn_t visc = Cs2 * (hydroVars.tau - 0.5); // kinematic viscosity
            LatticeDensity densityWall =
                3.0 * ((localIOlet->GetDensity(iolet.GetTimeStep()) * Cs2) +
                       visc * stress(-ioletNormal, hydroVars.tau, fNeqWall.data()));

            // Interpolate the density at the first fluid site accounting for the BC (equation 12).
            LatticeDensity densityBC =
                (densityWall + wallDistance * hVsecondFluid.density) / (1.0 + wallDistance);

            // Calculate the post-collision distributions at the second fluid site.
            collider.Collide(lbmParams, hVsecondFluid);

            // Calculate the distribution of the unstreamed direction (equation 11).
            fNew = site.template GetFOld<LatticeType>()[unstreamed] +
                   site.template GetFOld<LatticeType>()[direction] +
                   2.0 * LatticeType::EQMWEIGHTS[unstreamed] * (densityBC - hydroVars.density) -
                   hVsecondFluid.GetFPostCollision()[direction];
        }
        else
        {
            // Construct a HydroVars structure at the outer-wall node.
            FVector<LatticeType> fOuterWall{};
            VarsType hVouterWall(fOuterWall);

            // Calculate the required quantities at the outer-wall node.
            const LatticePosition sqBracket = momentumCorrection(
                cp.template as<double>(), -ioletNormal, hydroVars.tau, fNeqWall.data());
            distribn_t fNeqOuterWall;
            if (width == 2)
            {
                // Extrapolate the density and momentum (analogous to equation 18).
                hVouterWall.density = 4.0 * hVfirstFluid.density - 3.0 * hVsecondFluid.density;
                hVouterWall.momentum =
                    (hVfirstFluid.momentum * (8.0 * wallDistance - 8.0) +
                     hVsecondFluid.momentum * (9.0 - 6.0 * wallDistance) - sqBracket * 6.0) /
                    (1.0 + 2.0 * wallDistance);

                // Extrapolate the non-equilibrium distribution of the unstreamed direction
                // (analogous to equation 20).
                fNeqOuterWall = 4.0 * hVfirstFluid.GetFNeq()[unstreamed] -
                                3.0 * hVsecondFluid.GetFNeq()[unstreamed];
            }
            else if (width == 1)
            {
                // Extrapolate the density and momentum (equation B.4).
                hVouterWall.density = 3.0 * hVfirstFluid.density - 2.0 * hVsecondFluid.density;
                hVouterWall.momentum =
                    (hVfirstFluid.momentum * (6.0 * wallDistance - 3.0) +
                     hVsecondFluid.momentum * (4.0 - 4.0 * wallDistance) - sqBracket * 3.0) /
                    (1.0 + 2.0 * wallDistance);

                // Extrapolate the non-equilibrium distribution of the unstreamed direction
                // (equation B.4).
                fNeqOuterWall = 3.0 * hVfirstFluid.GetFNeq()[unstreamed] -
                                2.0 * hVsecondFluid.GetFNeq()[unstreamed];
            }
            else
            {
                // Extrapolate the density and momentum (equation 18).
                hVouterWall.density = 2.0 * hVfirstFluid.density - hVsecondFluid.density;
                hVouterWall.momentum =
                    (hVfirstFluid.momentum * 4.0 * wallDistance +
                     hVsecondFluid.momentum * (1.0 - 2.0 * wallDistance) - sqBracket) /
                    (1.0 + 2.0 * wallDistance);

                // Extrapolate the non-equilibrium distribution of the unstreamed direction
                // (equation 20).
                fNeqOuterWall =
                    2.0 * hVfirstFluid.GetFNeq()[unstreamed] - hVsecondFluid.GetFNeq()[unstreamed];
            }

            // Calculate the equilibrium distributions at the outer-wall node.
            LatticeType::CalculateFeq(hVouterWall.density, hVouterWall.momentum,
                                      hVouterWall.GetFEq());

            // Calculate the distribution of the unstreamed direction (equation 17).
            // Assumption 2 is applied here: A equals 1/tau times the identity matrix.
            fNew = hVouterWall.GetFEq()[unstreamed] + fNeqOuterWall * (1.0 - 1.0 / hydroVars.tau);
        }

        if (!std::isfinite(fNew))
            throw Exception() << "Yang pressure produced a non-finite distribution";
        *data.GetFNew(site.GetIndex() * LatticeType::NUMVECTORS + unstreamed) = fNew;
    }
    void PostStepLink(geometry::FieldData &, geometry::Site<geometry::FieldData> const &, Direction)
    {
    }

  private:
    static double stress(LatticePosition vec, double tau, double const *fNeq)
    {
        double value = 0;
        for (Direction k = 0; k < LatticeType::NUMVECTORS; ++k)
        {
            auto ck = LatticeType::VECTORS[k].template as<double>();
            double dot = Dot(vec, ck);
            double bk =
                (dot * dot - vec.GetMagnitudeSquared() * ck.GetMagnitudeSquared() / 3) / (2 * Cs2);
            value += bk * (fNeq[k] + fNeq[LatticeType::INVERSEDIRECTIONS[k]]);
        }
        return -value / (2 * tau);
    }
    static LatticePosition momentumCorrection(LatticePosition cp, LatticePosition normal,
                                              double tau, double const *fNeq)
    {
        auto tangential = cp - normal * Dot(cp, normal);
        double norm = tangential.GetMagnitude();
        auto tanVec = norm > 1e-14 ? tangential / norm : LatticePosition::Zero();
        double tanComp = Dot(cp, tanVec), normalComp = Dot(cp, normal);
        double g1 = 2 * tanComp * stress(tanVec, tau, fNeq), g2 = 2 * stress(cp, tau, fNeq);
        return tanVec * g1 + normal * (g2 - tanComp * g1) / normalComp;
    }
};
} // namespace hemelb::lb
#endif
