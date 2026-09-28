// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

#include <catch2/catch.hpp>

#include "geometry/GmyReadResult.h"
#include "geometry/GeometryReader.h"
#include "geometry/LookupTree.h"
#include "geometry/decomposition/BasicDecomposition.h"
#include "lb/lattices/D3Q15.h"
#include "reporting/Timers.h"
#include "tests/helpers/FolderTestFixture.h"

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

    TEST_CASE("Basic decomposition explains too many processes for the blocks", "[geometry]") {
        REQUIRE_THROWS_WITH(DecomposeCounts({5, 7}, 12, 3),
                            Catch::Contains("2 block(s) containing fluid") &&
                            Catch::Contains("running on 3 MPI processes") &&
                            Catch::Contains("mpirun -n 2"));
    }

    TEST_CASE("Basic decomposition rejects a site count overflow", "[geometry]") {
        REQUIRE_THROWS_AS(DecomposeCounts({std::numeric_limits<U64>::max(), 1}, 0, 2), Exception);
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "Basic decomposition assigns the provided large cylinder geometry to four ranks",
                     "[geometry]") {
        CopyResourceToTempdir("large_cylinder.gmy");
        MoveToTempdir();

        auto timings = std::make_unique<reporting::Timers>(Comms());
        geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
        auto result = reader.LoadAndDecompose("large_cylinder.gmy");
        const auto& tree = result.block_store->GetTree();
        REQUIRE(tree.levels[0].sites_per_node[0] == 5576);

        std::vector<proc_t> block_owners(result.GetBlockCount());
        const auto owners = geometry::decomposition::BasicDecomposition(result, 4)
                                .Decompose(tree, block_owners);
        REQUIRE(owners.size() == 20);
        REQUIRE(std::is_sorted(owners.begin(), owners.end()));
        REQUIRE(std::all_of(owners.begin(), owners.end(), [](int rank) { return rank >= 0 && rank < 4; }));
        for (int rank = 0; rank < 4; ++rank)
            REQUIRE(std::count(owners.begin(), owners.end(), rank) > 0);
        REQUIRE(std::accumulate(tree.levels[tree.n_levels].sites_per_node.begin(),
                                tree.levels[tree.n_levels].sites_per_node.end(), U64{0}) == 5576);
    }
}
