// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

#include <catch2/catch.hpp>

#include "geometry/GmyReadResult.h"
#include "geometry/LookupTree.h"
#include "geometry/decomposition/BasicDecomposition.h"

namespace hemelb::tests {
    namespace {
        std::vector<int> DecomposeCounts(const std::vector<U64>& counts, U64 total, int ranks) {
            geometry::GmyReadResult geometry({static_cast<U16>(counts.size()), 1, 1}, 1);
            geometry::octree::LookupTree tree(3);
            tree.levels[0].node_ids.push_back(0);
            tree.levels[0].sites_per_node.push_back(total);
            for (std::size_t i = 0; i < counts.size(); ++i) {
                tree.levels[3].node_ids.push_back(
                    geometry::octree::ijk_to_oct({static_cast<U16>(i), 0, 0}));
                tree.levels[3].sites_per_node.push_back(counts[i]);
            }
            std::vector<proc_t> owners(counts.size());
            return geometry::decomposition::BasicDecomposition(geometry, ranks).Decompose(tree, owners);
        }
    }

    TEST_CASE("Basic decomposition keeps 64-bit site count precision", "[geometry]") {
        const U64 large = U64{1} << 54;
        const auto owners = DecomposeCounts({large, 1, large + 1}, 2 * large + 2, 2);
        REQUIRE((owners == std::vector<int>{0, 0, 1}));
    }

    TEST_CASE("Basic decomposition gives every rank a block with skewed counts", "[geometry]") {
        const U64 large = U64{1} << 60;
        const std::vector<U64> counts{large, 1, 1, 1, 1, 1, 1, 1};
        const auto owners = DecomposeCounts(counts, large + 7, 4);
        REQUIRE(std::is_sorted(owners.begin(), owners.end()));
        for (int rank = 0; rank < 4; ++rank)
            REQUIRE(std::count(owners.begin(), owners.end(), rank) >= 1);
    }

    TEST_CASE("Basic decomposition rejects a site count overflow", "[geometry]") {
        REQUIRE_THROWS_AS(DecomposeCounts({std::numeric_limits<U64>::max(), 1}, 0, 2), Exception);
    }
}
