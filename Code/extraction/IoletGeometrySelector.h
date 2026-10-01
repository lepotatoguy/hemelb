// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.
#ifndef HEMELB_EXTRACTION_IOLETGEOMETRYSELECTOR_H
#define HEMELB_EXTRACTION_IOLETGEOMETRYSELECTOR_H
#include "extraction/GeometrySelector.h"

namespace hemelb::extraction {
    class IoletGeometrySelector : public GeometrySelector {
        bool inlet;
    public:
        explicit IoletGeometrySelector(bool inlet) : inlet(inlet) {}
        bool IsInlet() const { return inlet; }
        GeometrySelector* clone() const override { return new IoletGeometrySelector(inlet); }
    protected:
        bool IsWithinGeometry(IterableDataSource const& data, util::Vector3D<site_t> const& location) const override {
            return inlet ? data.IsInletSite(location) : data.IsOutletSite(location);
        }
    };
}
#endif
