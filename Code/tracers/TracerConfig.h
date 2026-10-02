// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_TRACERS_TRACERCONFIG_H
#define HEMELB_TRACERS_TRACERCONFIG_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "units.h"
#include "util/Vector3D.h"
namespace hemelb::tracers
{
struct Particle
{
    std::uint64_t id = 0;
    double radius = 0;
    LatticePosition position{}, velocity{};
    LatticeTimeStep created = 0;
    bool active = true;
};
struct BoundaryRule
{
    std::string kind, appliesTo;
    double range = 1, radius = 0;
    LatticePosition centre{};
};
struct TracerConfig
{
    std::vector<Particle> particles;
    std::vector<BoundaryRule> boundaries;
    LatticePosition sphereCentre{};
    double sphereRadius = 0, particleRadius = 0;
    unsigned emissionCount = 0;
    LatticeTimeStep emissionInterval = 1, nextEmission = 1, outputPeriod = 1;
    std::uint64_t nextId = 0, seed = 0;
};
} // namespace hemelb::tracers
#endif
