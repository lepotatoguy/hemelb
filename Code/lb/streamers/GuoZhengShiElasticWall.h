// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

// Parts adapted from HemePure (0bf67b16), Copyright (c) 2024,
// Centre for Computational Science (UCL). See COPYING.HemePure.
#ifndef HEMELB_LB_STREAMERS_GUOZHENGSHIELASTICWALL_H
#define HEMELB_LB_STREAMERS_GUOZHENGSHIELASTICWALL_H
#include "lb/streamers/Common.h"
#include "geometry/FieldData.h"

namespace hemelb::lb
{
inline double ElasticWallVelocityRatio(double density, double stiffness, double boundaryRatio)
{
    if (!std::isfinite(stiffness) || stiffness <= 0)
        throw Exception() << "Elastic wall stiffness must be positive";
    double extension = (density - 1) / (3 * stiffness);
    double radiusFactor = 1 + extension;
    return radiusFactor > 1 - boundaryRatio ? (boundaryRatio + extension) / radiusFactor : 0;
}
template <collision_type C> class GuoZhengShiElasticWallLink
{
    C &collider;

  public:
    using CollisionType = C;
    using LatticeType = typename C::LatticeType;
    using VarsType = typename C::VarsType;
    GuoZhengShiElasticWallLink(C &c, InitParams &init) : collider(c)
    {
        if (init.lbmParams->elasticWallStiffness <= 0)
            throw Exception() << "GZSElastic requires simulation/elastic_wall_stiffness";
    }
    void StreamLink(LbmParameters const *params, geometry::FieldData &data,
                    geometry::Site<geometry::FieldData> const &site, VarsType &vars,
                    Direction direction)
    {
        FVector<LatticeType> f{};
        VarsType ghost(f);
        ghost.density = vars.density;
        ghost.tau = vars.tau;
        ghost.momentum =
            vars.momentum * ElasticWallVelocityRatio(vars.density, params->elasticWallStiffness,
                                                     params->boundaryVelocityRatio);
        ghost.velocity = ghost.momentum / ghost.density;
        LatticeType::CalculateFeq(ghost.density, ghost.momentum, ghost.GetFEq());
        for (Direction d = 0; d < LatticeType::NUMVECTORS; ++d)
        {
            ghost.SetFNeq(d, vars.GetFNeq()[d]);
            f[d] = ghost.GetFEq()[d] + ghost.GetFNeq()[d];
        }
        collider.Collide(params, ghost);
        *data.GetFNew(site.GetIndex() * LatticeType::NUMVECTORS +
                      LatticeType::INVERSEDIRECTIONS[direction]) =
            ghost.GetFPostCollision()[LatticeType::INVERSEDIRECTIONS[direction]];
    }
    void PostStepLink(geometry::FieldData &, geometry::Site<geometry::FieldData> const &, Direction)
    {
    }
};
} // namespace hemelb::lb
#endif
