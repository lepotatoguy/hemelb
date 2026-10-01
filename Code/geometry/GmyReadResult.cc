// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "geometry/GmyReadResult.h"
#include "geometry/LookupTree.h"
#include "io/formats/geometry.h"

#include <limits>

namespace hemelb::geometry {
    namespace {
        site_t CheckedBlockCount(Vec16 const& dimensions) {
            std::uint64_t count = 1;
            for (auto dimension: dimensions) {
                if (dimension == 0)
                    throw Exception() << "Geometry block dimensions must be positive";
                if (count > std::uint64_t(std::numeric_limits<site_t>::max()) / dimension)
                    throw Exception() << "Geometry block count overflows site_t";
                count *= dimension;
            }
            return static_cast<site_t>(count);
        }

        site_t CheckedSitesPerBlock(U16 blockSize) {
            if (blockSize == 0)
                throw Exception() << "Geometry block size must be positive";
            auto size = std::uint64_t(blockSize);
            auto count = size * size * size;
            if (count > std::uint64_t(std::numeric_limits<site_t>::max()))
                throw Exception() << "Geometry site count per block overflows site_t";
            return static_cast<site_t>(count);
        }
    }

    GmyReadResult::GmyReadResult(const Vec16& dimensionsInBlocks, U16 blockSize) :
            dimensionsInBlocks(dimensionsInBlocks), blockSize(blockSize),
            blockCount(CheckedBlockCount(dimensionsInBlocks)),
            sitesPerBlock(CheckedSitesPerBlock(blockSize))
    {
    }

    GmyReadResult::~GmyReadResult() = default;

    site_t GmyReadResult::FindSiteIndexInBlock(site_t fluidSiteBlock, site_t fluidSitesToPass) const
    {
        site_t siteIndex = 0;
        while (true)
        {
            // We keep going through the sites on the block until we've passed as many fluid
            // sites as we need to.
            if (Blocks[fluidSiteBlock].Sites[siteIndex].targetProcessor != SITE_OR_BLOCK_SOLID)
            {
                fluidSitesToPass--;
            }
            if (fluidSitesToPass < 0)
            {
                break;
            }
            siteIndex++;
        }
        return siteIndex;
    }

    site_t GmyReadResult::FindFluidSiteIndexInBlock(site_t fluidSiteBlock, site_t neighbourSiteId) const
    {
        auto& sites = Blocks[fluidSiteBlock].Sites;
        return std::count_if(
            &sites[0], &sites[neighbourSiteId], [](GeometrySite const& s) {
                return s.isFluid;
            }
        );
    }
}
