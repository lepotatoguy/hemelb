// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <memory>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <limits>

#include <zlib.h>

#include <catch2/catch.hpp>

#include "configuration/SimConfig.h"
#include "geometry/Domain.h"
#include "geometry/LookupTree.h"
#include "geometry/GeometryReader.h"
#include "lb/lattices/D3Q15.h"
#include "reporting/Timers.h"
#include "resources/Resource.h"

#include "tests/helpers/FourCubeLatticeData.h"
#include "tests/helpers/FolderTestFixture.h"
#include "tests/helpers/LaddFail.h"
#include "tests/helpers/EqualitySiteData.h"

namespace hemelb::tests
{
    namespace {
      void WriteXdrUInt(std::fstream& file, std::streamoff offset, std::uint32_t value) {
        const char bytes[] = {
            static_cast<char>(value >> 24), static_cast<char>(value >> 16),
            static_cast<char>(value >> 8), static_cast<char>(value)};
        file.seekp(offset);
        file.write(bytes, sizeof(bytes));
        REQUIRE(file.good());
      }

      void WriteXdrUInt(std::vector<char>& data, std::size_t offset, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte)
          data[offset + byte] = static_cast<char>(value >> (24 - 8 * byte));
      }
    }

    TEST_CASE("GmyReadResult checks dimensions before allocating blocks", "[geometry]") {
      geometry::GmyReadResult small({3, 4, 5}, 8);
      REQUIRE(small.GetBlockCount() == 60);
      REQUIRE(small.GetSitesPerBlock() == 512);
      REQUIRE_THROWS_WITH(geometry::GmyReadResult({0, 1, 1}, 8),
                          Catch::Matchers::Contains("dimensions must be positive"));
      geometry::GmyReadResult large({65535, 65535, 1}, 8);
      REQUIRE(large.GetBlockCount() == U64(65535) * 65535);
      REQUIRE(large.Blocks.empty());
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader rejects a header larger than its file", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();
      std::fstream geometryFile("large_cylinder.gmy", std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(geometryFile.is_open());
      WriteXdrUInt(geometryFile, 12, 65535);
      WriteXdrUInt(geometryFile, 16, 65535);
      geometryFile.close();

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("shorter than its block header"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader rejects impossible decompressed length", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();
      std::fstream geometryFile("large_cylinder.gmy", std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(geometryFile.is_open());
      WriteXdrUInt(geometryFile, 40, std::numeric_limits<std::uint32_t>::max());
      geometryFile.close();

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("declares too much uncompressed data"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader rejects truncated block data", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();
      const auto size = std::filesystem::file_size("large_cylinder.gmy");
      std::filesystem::resize_file("large_cylinder.gmy", size - 1);

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("shorter than its declared block data"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader rejects corrupted compressed data", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();
      std::fstream geometryFile("large_cylinder.gmy", std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(geometryFile.is_open());
      geometryFile.seekp(32 + 20 * 12);
      const char badZlibHeader = 0;
      geometryFile.write(&badZlibHeader, 1);
      REQUIRE(geometryFile.good());
      geometryFile.close();

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("Decompression error for geometry block 0"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader reports a truncated site record", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();
      std::ifstream input("large_cylinder.gmy", std::ios::binary);
      REQUIRE(input.is_open());
      std::vector<char> sample(std::istreambuf_iterator<char>{input}, {});
      input.close();

      // Replace the first block in the sample with a compressed fluid site type
      // that has no following link data. Keep the remaining sample blocks intact.
      const char incompleteSite[] = {0, 0, 0, 1};
      std::vector<unsigned char> compressed(compressBound(sizeof(incompleteSite)));
      uLongf compressedLength = compressed.size();
      REQUIRE(compress2(compressed.data(), &compressedLength,
                        reinterpret_cast<const Bytef*>(incompleteSite), sizeof(incompleteSite),
                        Z_BEST_COMPRESSION) == Z_OK);
      constexpr std::size_t bodyStart = 32 + 20 * 12;
      constexpr std::size_t originalFirstBlockLength = 978;
      WriteXdrUInt(sample, 36, compressedLength);
      WriteXdrUInt(sample, 40, sizeof(incompleteSite));
      std::vector<char> damaged(sample.begin(), sample.begin() + bodyStart);
      damaged.insert(damaged.end(), compressed.begin(), compressed.begin() + compressedLength);
      damaged.insert(damaged.end(), sample.begin() + bodyStart + originalFirstBlockLength, sample.end());
      std::ofstream output("large_cylinder.gmy", std::ios::binary | std::ios::trunc);
      output.write(damaged.data(), damaged.size());
      REQUIRE(output.good());
      output.close();

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("Malformed geometry block 0 site 0: Truncated XDR data"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture,
                     "GeometryReader rejects a compressed block larger than its buffer", "[geometry]") {
      CopyResourceToTempdir("large_cylinder.gmy");
      MoveToTempdir();

      // Change the first block length in the sample GMY header to 64 MiB + 1.
      std::fstream geometryFile("large_cylinder.gmy", std::ios::in | std::ios::out | std::ios::binary);
      REQUIRE(geometryFile.is_open());
      geometryFile.seekp(36);
      const char oversizedLength[] = {0x04, 0x00, 0x00, 0x01};
      geometryFile.write(oversizedLength, sizeof(oversizedLength));
      REQUIRE(geometryFile.good());
      geometryFile.close();

      auto timings = std::make_unique<reporting::Timers>();
      geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), *timings, Comms());
      REQUIRE_THROWS_WITH(reader.LoadAndDecompose("large_cylinder.gmy"),
                          Catch::Matchers::Contains("exceeds the 64 MiB read buffer"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "GeometryReaderTests") {
      auto timings = std::make_unique<reporting::Timers>();
      
      auto reader = std::make_unique<geometry::GeometryReader>(lb::D3Q15::GetLatticeInfo(),
							       *timings,
							       Comms());
      CopyResourceToTempdir("four_cube.xml");
      CopyResourceToTempdir("four_cube.gmy");
      auto simConfig = configuration::SimConfig::New("four_cube.xml");

      SECTION("TestRead") {
	LADD_FAIL();
	reader->LoadAndDecompose(simConfig.GetDataFilePath());
      }

      SECTION("TestSameAsFourCube") {
	LADD_FAIL();
	auto fourCube = std::unique_ptr<FourCubeLatticeData>{FourCubeLatticeData::Create(Comms())};
	auto readResult = reader->LoadAndDecompose(simConfig.GetDataFilePath());
    auto&& dom = fourCube->GetDomain();

	for (site_t i = 1; i < 5; i++) {
	  for (site_t j = 1; j < 5; j++) {
	    bool isWallSite = (i == 1 || i == 4 || j == 1 || j == 4);

	    for (site_t k = 1; k < 5; k++) {
	      //std::cout << i << "," << j << "," << k << " > " << std::setbase(8) << fourCube->GetSiteData(i*16+j*4+k) << " : " << globalLattice->GetSiteData(i,j,k) << std::endl;
	      util::Vector3D<site_t> location{i, j, k};
	      site_t siteIndex = dom.GetGlobalNoncontiguousSiteIdFromGlobalCoords(location);

	      hemelb::geometry::SiteData siteData(readResult.Blocks[0].Sites[siteIndex]);
	      REQUIRE(fourCube->GetSite(dom.GetContiguousSiteId(location)).GetSiteData() == siteData);
	      //                  CPPUNIT_ASSERT_EQUAL(fourCube->GetSite(fourCube->GetContiguousSiteId(location)).GetSiteData().GetOtherRawData(),
	      //                                       siteData.GetOtherRawData());
	      //
	      //                  CPPUNIT_ASSERT_EQUAL(fourCube->GetSite(fourCube->GetContiguousSiteId(location)).GetSiteData().GetWallIntersectionData(),
	      //                                       siteData.GetWallIntersectionData());

	      REQUIRE(isWallSite == readResult.Blocks[0].Sites[siteIndex].wallNormalAvailable);

	      if (isWallSite) {
		/// @todo: #597 use CPPUNIT_ASSERT_EQUAL directly (having trouble with Vector3D templated over different types at the minute)
		/// CPPUNIT_ASSERT_EQUAL(fourCube->GetSite(fourCube->GetContiguousSiteId(location)).GetWallNormal(), readResult.Blocks[0].Sites[siteIndex].wallNormal);
		REQUIRE(fourCube->GetSite(dom.GetContiguousSiteId(location)).GetWallNormal()
			== readResult.Blocks[0].Sites[siteIndex].wallNormal.as<double>());
	      }
	    }
	  }
	}

      }

    }
    TEST_CASE("Sparse octree builds only active leaves in a large box", "[geometry]") {
        const Vec16 dims{65535, 65535, 1};
        std::map<U64, site_t> counts{{0, 1}, {U64(65535) * 65535 - 1, 2}};
        auto tree = geometry::octree::build_block_tree(dims, counts);
        REQUIRE(tree.levels.back().node_ids.size() == 2);
        REQUIRE(tree.levels[0].sites_per_node[0] == 3);
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Geometry reader ignores gmy+ weights", "[geometry]") {
        CopyResourceToTempdir("large_cylinder.gmy");
        MoveToTempdir();
        std::ifstream input("large_cylinder.gmy", std::ios::binary);
        std::vector<char> original(std::istreambuf_iterator<char>{input}, {});
        std::ofstream output("weighted.gmy+", std::ios::binary);
        output.write(original.data(), 32);
        const char weight[]{0, 0, 0, 17};
        for (int block = 0; block < 20; ++block) {
            output.write(original.data() + 32 + 12 * block, 4);
            output.write(weight, 4);
            output.write(original.data() + 36 + 12 * block, 8);
        }
        output.write(original.data() + 272, original.size() - 272);
        output.close();
        reporting::Timers times, weightedTimes;
        geometry::GeometryReader reader(lb::D3Q15::GetLatticeInfo(), times, Comms(), false);
        geometry::GeometryReader weightedReader(lb::D3Q15::GetLatticeInfo(), weightedTimes, Comms(), false);
        auto plain = reader.LoadAndDecompose("large_cylinder.gmy");
        auto weighted = weightedReader.LoadAndDecompose("weighted.gmy+");
        REQUIRE(plain.Blocks.size() == weighted.Blocks.size());
        for (auto const& [id, block]: plain.Blocks) {
            auto const& other = weighted.Blocks.at(id);
            REQUIRE(block.Sites.size() == other.Sites.size());
            for (std::size_t i = 0; i < block.Sites.size(); ++i) {
                auto const& a = block.Sites[i]; auto const& b = other.Sites[i];
                REQUIRE(a.isFluid == b.isFluid);
                REQUIRE(a.targetProcessor == b.targetProcessor);
                if (!a.isFluid) continue;
                REQUIRE(a.targetProcessor == 0);
                REQUIRE(a.wallNormalAvailable == b.wallNormalAvailable);
                if (a.wallNormalAvailable) REQUIRE(a.wallNormal == b.wallNormal);
                REQUIRE(a.links.size() == b.links.size());
                for (std::size_t j = 0; j < a.links.size(); ++j) {
                    REQUIRE(a.links[j].type == b.links[j].type);
                    REQUIRE(a.links[j].ioletId == b.links[j].ioletId);
                    REQUIRE(a.links[j].distanceToIntersection == b.links[j].distanceToIntersection);
                }
            }
        }
        REQUIRE(times.parmetis().Get() == 0);
    }

}
