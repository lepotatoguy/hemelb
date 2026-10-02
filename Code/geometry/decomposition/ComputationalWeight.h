// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_GEOMETRY_DECOMPOSITION_COMPUTATIONALWEIGHT_H
#define HEMELB_GEOMETRY_DECOMPOSITION_COMPUTATIONALWEIGHT_H
#include <algorithm>
#include <cmath>
#include "Exception.h"
namespace hemelb::geometry::decomposition
{
// ParMETIS accepts positive integers. A common scale preserves supplied
// per-site cost ratios to one part in a million of the largest cost.
inline int ComputationalSiteWeight(double cost, double largestCost)
{
    if (!std::isfinite(cost) || cost <= 0 || !std::isfinite(largestCost) || largestCost < cost)
        throw Exception() << "Invalid computational site cost";
    return std::max(1, int(std::round(1000000 * cost / largestCost)));
}
} // namespace hemelb::geometry::decomposition
#endif
