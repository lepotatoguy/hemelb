// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.
#ifndef HEMELB_EXTRACTION_SPHEREGEOMETRYSELECTOR_H
#define HEMELB_EXTRACTION_SPHEREGEOMETRYSELECTOR_H
#include <cmath>
#include "Exception.h"
#include "extraction/GeometrySelector.h"

namespace hemelb::extraction
{
class SphereGeometrySelector : public GeometrySelector
{
    PhysicalPosition centre;
    PhysicalDistance radius;
    bool surface;

  public:
    SphereGeometrySelector(PhysicalPosition point, PhysicalDistance r, bool wallOnly = false)
        : centre(point), radius(r), surface(wallOnly)
    {
        if (!std::isfinite(r) || r <= 0 || !std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            !std::isfinite(point.z()))
            throw Exception() << "Sphere centre must be finite and radius finite and positive";
    }
    PhysicalPosition const &GetPoint() const { return centre; }
    PhysicalDistance GetRadius() const { return radius; }
    bool IsSurfaceOnly() const { return surface; }
    GeometrySelector *clone() const override { return new SphereGeometrySelector(*this); }

  protected:
    bool IsWithinGeometry(IterableDataSource const &data,
                          util::Vector3D<site_t> const &location) const override
    {
        auto point = data.GetOrigin().template as<double>() +
                     location.template as<double>() * data.GetVoxelSize();
        return (!surface || data.IsWallSite(location)) &&
               (point - centre).GetMagnitudeSquared() <= radius * radius;
    }
};
} // namespace hemelb::extraction
#endif
