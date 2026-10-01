// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <cmath>
#include <list>
#include <algorithm>
#include <limits>
#include <utility>
#include <filesystem>
#include <cstring>
#include <zlib.h>

#include "io/formats/geometry.h"
#include "io/readers/XdrMemReader.h"
#include "geometry/decomposition/BasicDecomposition.h"
#include "geometry/decomposition/OptimisedDecomposition.h"
#include "geometry/GeometryReader.h"
#include "geometry/LookupTree.h"
#include "net/net.h"
#include "net/SparseExchange.h"
#include "net/IOCommunicator.h"
#include "log/Logger.h"
#include "util/span.h"
#include "util/numerical.h"
#include "util/Iterator.h"
#include "constants.h"

namespace hemelb::geometry
{
    namespace fmt = io::formats;
    using gmy = fmt::geometry;
    namespace {
      constexpr std::size_t MAX_GMY_BUFFER_SIZE = 64U * (std::size_t(1) << 20);
    }

    // Helper for checking that integers are allowed values for enums.
    template <typename Enum, Enum... allowed>
    struct EnumValidator {
    private:
        using INT = std::underlying_type_t<Enum>;

        // Want to turn the parameter pack into something iterable at
        // constexpr time, such as an initializer_list
        static constexpr bool IsValid(INT raw, std::initializer_list<Enum> vals) {
            for (auto& val: vals) {
                if (val == static_cast<Enum>(raw))
                    return true;
            }
            return false;
        }

    public:
        static Enum Run(INT raw) {
            if (IsValid(raw, {allowed...})) {
                return static_cast<Enum>(raw);
            } else {
                throw Exception() << "Invalid value for enum " << raw;
            }
        }
    };

    using SiteTypeValidator =  EnumValidator<gmy::SiteType,
            gmy::SiteType::SOLID,
            gmy::SiteType::FLUID>;
    using CutTypeValidator = EnumValidator<gmy::CutType,
            gmy::CutType::NONE,
            gmy::CutType::WALL,
            gmy::CutType::INLET,
            gmy::CutType::OUTLET>;
    using WallNormalAvailabilityValidator = EnumValidator<gmy::WallNormalAvailability,
            gmy::WallNormalAvailability::NOT_AVAILABLE,
            gmy::WallNormalAvailability::AVAILABLE>;

    GeometryReader::GeometryReader(const lb::LatticeInfo& latticeInfo,
                                   reporting::Timers &atimings, net::IOCommunicator ioComm, bool optimise) :
            latticeInfo(latticeInfo), computeComms(std::move(ioComm)), timings(atimings), optimise(optimise)
    {
    }

    GeometryReader::~GeometryReader()
    = default;

    GmyReadResult GeometryReader::LoadAndDecompose(const std::string& dataFilePath)
    {
        timings.fileRead().Start();

        headerRecordLength = std::filesystem::path(dataFilePath).extension() == ".gmy+" ? 16 : gmy::HeaderRecordLength;
        file = net::MpiFile::Open(computeComms, dataFilePath, MPI_MODE_RDONLY, MPI_INFO_NULL);
        fluidSitesOnEachBlock.clear();
        blockMetadata.clear();
        compressedCache.clear();
        timings.geometryBlocksRead = 0;
        timings.geometryBytesRead = 0;
        log::Logger::Log<log::Debug, log::OnePerCore>("Reading file preamble");
        GmyReadResult geometry = ReadPreamble();

        log::Logger::Log<log::Debug, log::OnePerCore>("Reading file header");
        ReadHeader(geometry.GetBlockCount(), geometry.GetSitesPerBlock());
        timings.fileRead().Stop();

        {
            timings.initialDecomposition().Start();


            log::Logger::Log<log::Info, log::Singleton>("Creating block-level octree");
            auto blockTree = octree::build_block_tree(
                    geometry.GetBlockDimensions().as<octree::U16>(),
                    fluidSitesOnEachBlock
            );
            nFluidBlocks = blockTree.levels.back().node_ids.size();
            log::Logger::Log<log::Info, log::Singleton>(
                "Geometry has %lu / %ld active blocks, total %ld sites",
                nFluidBlocks,
                geometry.GetBlockCount(),
                blockTree.levels[0].sites_per_node[0]
            );

            // Get an initial base-level decomposition of the domain macro-blocks over processors.
            // This will later be improved upon by ParMetis.
            log::Logger::Log<log::Info, log::Singleton>("Beginning initial decomposition");
            decomposition::BasicDecomposition basicDecomposer(geometry,
                                                              computeComms.Size());

            // This vector only has entries for blocks that have a least one fluid site
            procForBlockOct = basicDecomposer.Decompose(blockTree);
            geometry.block_store = std::make_unique<octree::DistributedStore>(
                    geometry.GetSitesPerBlock(),
                    std::move(blockTree),
                    procForBlockOct,
                    computeComms
            );
            if constexpr (build_info::VALIDATE_GEOMETRY) {
                log::Logger::Log<log::Info, log::Singleton>("Validating initial decomposition");
                basicDecomposer.Validate(procForBlockOct, computeComms);
            }

            timings.initialDecomposition().Stop();
        }

        timings.fileRead().Start();
        {
          std::vector<U64> blocks_wanted;
          blocks_wanted.reserve((2*nFluidBlocks) / computeComms.Size());
          for (U64 i = 0; i < nFluidBlocks; ++i) {
            if (procForBlockOct[i] == computeComms.Rank())
              blocks_wanted.push_back(i);
          }
          ReadInBlocksWithHalo(geometry, blocks_wanted);
        }
        if constexpr (build_info::VALIDATE_GEOMETRY) {
            ValidateGeometry(geometry);
        }
        timings.fileRead().Stop();

        if (optimise) {
            timings.domainDecomposition().Start();
            OptimiseDomainDecomposition(geometry);
            timings.domainDecomposition().Stop();
        } else {
            log::Logger::Log<log::Info, log::Singleton>("Using site-weighted octree decomposition.");
            auto const& tree = geometry.block_store->GetTree();
            for (auto& [id, block]: geometry.Blocks) {
                auto leaf = tree.GetLeaf(geometry.GetBlockCoordinatesFromBlockId(id));
                auto rank = procForBlockOct[leaf.index()];
                for (auto& site: block.Sites)
                    if (site.isFluid) site.targetProcessor = rank;
            }
        }
        if constexpr (build_info::VALIDATE_GEOMETRY) ValidateGeometry(geometry);
        compressedCache.clear();
        return geometry;
    }

    std::vector<std::byte> GeometryReader::ReadAllProcesses(std::size_t start, unsigned nBytes)
    {
        // result
        std::vector<std::byte> buffer(nBytes);
        auto sp = to_span(buffer);
        if (computeComms.OnIORank()) file.ReadAt(start, sp);
        computeComms.Broadcast(sp, computeComms.GetIORank());
        return buffer;
    }

    /**
     * Read in the section at the beginning of the config file.
     */
    GmyReadResult GeometryReader::ReadPreamble()
    {
      MPI_Offset fileSize = 0;
      if (computeComms.OnIORank())
        fileSize = file.GetSize();
      computeComms.Broadcast(fileSize, computeComms.GetIORank());
      if (fileSize < static_cast<MPI_Offset>(gmy::PreambleLength))
        throw Exception() << "Geometry file is shorter than its preamble";

      std::vector<std::byte> preambleBuffer = ReadAllProcesses(0, gmy::PreambleLength);

      // Create an Xdr translator based on the read-in data.
      auto preambleReader = io::XdrMemReader(preambleBuffer.data(),
                                                           gmy::PreambleLength);

      uint32_t hlbMagicNumber, gmyMagicNumber, version;
      // Read in housekeeping values
      preambleReader.read(hlbMagicNumber);
      preambleReader.read(gmyMagicNumber);
      preambleReader.read(version);

      // Check the value of the HemeLB magic number.
      if (hlbMagicNumber != fmt::HemeLbMagicNumber)
      {
        throw Exception() << "This file does not start with the HemeLB magic number."
            << " Expected: " << unsigned(fmt::HemeLbMagicNumber)
            << " Actual: " << hlbMagicNumber;
      }

      // Check the value of the geometry file magic number.
      if (gmyMagicNumber != gmy::MagicNumber)
      {
        throw Exception() << "This file does not have the geometry magic number."
            << " Expected: " << unsigned(gmy::MagicNumber)
            << " Actual: " << gmyMagicNumber;
      }

      if (version != gmy::VersionNumber)
      {
        throw Exception() << "Version number incorrect."
            << " Supported: " << unsigned(gmy::VersionNumber)
            << " Input: " << version;
      }

      // Variables we'll read.
      // We use temporary vars here, as they must be the same size as the type in the file
      // regardless of the internal type used.
      uint32_t blocksX, blocksY, blocksZ, blockSize;

      auto read_check = [&](uint32_t& var) {
          preambleReader.read(var);
          if (var > std::uint32_t(std::numeric_limits<std::uint16_t>::max())) {
              throw Exception() << "size greater than 2^16 - 1!";
          }
      };
      // Read in the values.
      read_check(blocksX);
      read_check(blocksY);
      read_check(blocksZ);
      read_check(blockSize);

      if (blocksX == 0 || blocksY == 0 || blocksZ == 0 || blockSize == 0)
        throw Exception() << "Geometry dimensions and block size must be positive";

      const auto blockCount = std::uint64_t(blocksX) * blocksY * blocksZ;
      const auto headerBytes = blockCount * headerRecordLength;
      if (std::uint64_t(fileSize) < gmy::PreambleLength + headerBytes)
        throw Exception() << "Geometry file is shorter than its block header";

      // Read the padding unsigned int.
      unsigned paddingValue;
      preambleReader.read(paddingValue);

      return {Vec16(blocksX, blocksY, blocksZ), U16(blockSize)};
    }

    /**
     * Read the header section, with minimal information about each block.
     *
     * Results are placed in the member arrays fluidSitesPerBlock,
     * bytesPerCompressedBlock and bytesPerUncompressedBlock.
     */
    void GeometryReader::ReadHeader(site_t blockCount, site_t sitesPerBlock)
    {
      constexpr U64 chunkBlocks = 100000;
      std::uint64_t offset = gmy::PreambleLength + std::uint64_t(GetHeaderLength(blockCount));
      U64 active = 0;
      for (U64 first = 0; first < U64(blockCount); first += chunkBlocks) {
        const auto count = std::min(chunkBlocks, U64(blockCount) - first);
        auto buffer = ReadAllProcesses(gmy::PreambleLength + first * headerRecordLength,
                                       count * headerRecordLength);
        io::XdrMemReader reader(buffer);
        for (U64 i = 0; i < count; ++i) {
          unsigned sites, bytes, uncompressed;
          reader.read(sites);
          if (headerRecordLength == 16) reader.read<unsigned>();
          reader.read(bytes); reader.read(uncompressed);
          auto id = first + i;
          if (sites > sitesPerBlock) throw Exception() << "Geometry block " << id << " has more fluid sites than sites per block";
          if (bytes > MAX_GMY_BUFFER_SIZE) throw Exception() << "Compressed geometry block " << id << " exceeds the 64 MiB read buffer";
          if (sites && (!bytes || !uncompressed)) throw Exception() << "Geometry block " << id << " has missing compressed data";
          if (!sites && (bytes || uncompressed)) throw Exception() << "Solid geometry block " << id << " declares block data";
          if (uncompressed > U64(sitesPerBlock) * gmy::MaxFluidSiteRecordLength)
              throw Exception() << "Geometry block " << id << " declares too much uncompressed data";
          if (sites) {
            fluidSitesOnEachBlock.emplace(id, sites);
            blockMetadata.emplace(id, BlockMetadata{bytes, uncompressed, offset, int(active % computeComms.Size())});
            ++active;
          }
          offset += bytes;
        }
      }
      if (offset > U64(file.GetSize())) throw Exception() << "Geometry file is shorter than its declared block data";
      if (!active) throw Exception() << "Geometry contains no fluid blocks";
    }

    auto GeometryReader::ReadCompressedBlockData(std::vector<U64> const& wanted) -> block_cache {
      std::map<int, std::vector<U64>> requests;
      for (auto id: wanted) requests[blockMetadata.at(id).reader].push_back(id);
      net::sparse_exchange<U64> query(computeComms, 210);
      for (auto const& [rank, ids]: requests) query.send(to_const_span(ids), rank);
      std::map<int, std::vector<U64>> incoming;
      query.receive([&](int rank, int size) { auto& ids = incoming[rank]; ids.resize(size); return ids.data(); },
                    [](int, U64*) {});
      net::sparse_exchange<std::byte> delivery(computeComms, 211);
      std::list<std::vector<std::byte>> messages;
      for (auto const& [rank, ids]: incoming) {
        for (auto id: ids) {
          auto [it, fresh] = compressedCache.try_emplace(id);
          auto const& meta = blockMetadata.at(id);
          if (fresh) {
            it->second.resize(meta.compressed);
            timings.readBlock().Start();
            file.ReadAt(meta.offset, to_span(it->second));
            ++timings.geometryBlocksRead;
            timings.geometryBytesRead += meta.compressed;
            timings.readBlock().Stop();
          }
          auto& message = messages.emplace_back(sizeof(U64) + it->second.size());
          std::memcpy(message.data(), &id, sizeof(id));
          std::memcpy(message.data() + sizeof(id), it->second.data(), it->second.size());
          delivery.send(to_const_span(message), rank);
        }
      }
      block_cache ans;
      std::vector<std::byte> recv;
      delivery.receive([&](int, int size) { recv.resize(size); return recv.data(); },
                       [&](int, std::byte*) {
                         if (recv.size() < sizeof(U64)) throw Exception() << "Truncated geometry delivery";
                         U64 id; std::memcpy(&id, recv.data(), sizeof(id));
                         if (recv.size() != sizeof(id) + blockMetadata.at(id).compressed)
                             throw Exception() << "Invalid geometry delivery size";
                         ans[id] = std::vector<std::byte>(recv.begin() + sizeof(id), recv.end());
                       });
      return ans;
    }

    void GeometryReader::ReadInBlocksWithHalo(GmyReadResult& geometry,
                                              const std::vector<U64>& blocksWanted)
    {
      // Create a list of which blocks to read in.
      timings.readBlocksPrelim().Start();

      // Populate the list of blocks to read (including a halo one block wide around all
      // local blocks).
      log::Logger::Log<log::Debug, log::OnePerCore>("Determining blocks to read");

      auto halo_wanted = DecideWhichBlocksToReadIncludingHalo(geometry, blocksWanted);

      // GMY file indexs of blocks we want.
      std::vector<std::size_t> wanted_gmys;
      wanted_gmys.reserve(halo_wanted.size());
      auto&& tree = geometry.block_store->GetTree();
      for (auto idx: halo_wanted) {
          auto ijk = tree.GetLeafCoords(idx);
          wanted_gmys.push_back(geometry.GetBlockIdFromBlockCoordinates(ijk));
      }
      std::sort(wanted_gmys.begin(), wanted_gmys.end());
      for (auto it = geometry.Blocks.begin(); it != geometry.Blocks.end();) {
        if (!std::binary_search(wanted_gmys.begin(), wanted_gmys.end(), it->first)) it = geometry.Blocks.erase(it);
        else ++it;
      }
      std::vector<U64> missing;
      for (auto id: wanted_gmys) if (!geometry.Blocks.contains(id)) missing.push_back(id);
      auto compressed_block_data = ReadCompressedBlockData(missing);
      for (auto& [gmy_idx, data]: compressed_block_data) {
        DeserialiseBlock(geometry, data, gmy_idx);
      }
      timings.readBlocksPrelim().Stop();
    }

    void GeometryReader::DeserialiseBlock(
        GmyReadResult& geometry, std::vector<std::byte> const& compressedBlockData,
        site_t block_gmy
    ) {
        timings.readParse().Start();
        // Create an Xdr interpreter.
        auto blockData = DecompressBlockData(compressedBlockData,
                                             blockMetadata.at(block_gmy).uncompressed, block_gmy);
        if (blockData.empty())
          throw Exception() << "Geometry block " << block_gmy << " has no site data";
        io::XdrMemReader lReader(blockData);

        ParseBlock(geometry, block_gmy, lReader);
        if (lReader.GetPosition() != blockData.size())
          throw Exception() << "Geometry block " << block_gmy << " has trailing site data";

        // If debug-level logging, check that we've read in as many sites as anticipated.
        if constexpr (build_info::VALIDATE_GEOMETRY) {
          // Count the sites read,
          site_t numSitesRead = 0;
          for (site_t site = 0; site < geometry.GetSitesPerBlock(); ++site)
          {
            if (geometry.Blocks[block_gmy].Sites[site].targetProcessor != SITE_OR_BLOCK_SOLID)
            {
              ++numSitesRead;
            }
          }
          // Compare with the sites we expected to read.
          if (numSitesRead != fluidSitesOnEachBlock.at(block_gmy))
          {
            log::Logger::Log<log::Error, log::OnePerCore>("Was expecting %i fluid sites on block %i but actually read %i",
                                                          fluidSitesOnEachBlock.at(block_gmy),
                                                          block_gmy,
                                                          numSitesRead);
          }
        }
      timings.readParse().Stop();
    }

    std::vector<std::byte> GeometryReader::DecompressBlockData(const std::vector<std::byte>& compressed,
                                                          const unsigned int uncompressedBytes, site_t blockGmy)
    {
      timings.unzip().Start();
      if (compressed.empty() || uncompressedBytes == 0)
        throw Exception() << "Geometry block " << blockGmy << " has empty compressed or uncompressed data";

      // Set up the buffer for decompressed data. We know how long the the data is
      std::vector<std::byte> uncompressed(uncompressedBytes);

      // Set up the inflator
      z_stream stream{};
      stream.avail_in = compressed.size();
      stream.next_in = reinterpret_cast<unsigned char*>(const_cast<std::byte*>(compressed.data()));

      int ret = inflateInit(&stream);
      if (ret != Z_OK)
        throw Exception() << "Decompression error for geometry block " << blockGmy;

      stream.avail_out = uncompressed.size();
      stream.next_out = reinterpret_cast<unsigned char*>(uncompressed.data());

      ret = inflate(&stream, Z_FINISH);
      const bool complete = ret == Z_STREAM_END && stream.total_out == uncompressedBytes && stream.avail_in == 0;
      const int endRet = inflateEnd(&stream);
      if (!complete || endRet != Z_OK)
        throw Exception() << "Decompression error for geometry block " << blockGmy;

      timings.unzip().Stop();
      return uncompressed;
    }

    void GeometryReader::ParseBlock(GmyReadResult& geometry, const site_t block,
                                    io::XdrReader& reader)
    {
      // Clear any previous parsed sites before decoding this block.
      geometry.Blocks[block].Sites.clear();

      for (site_t localSiteIndex = 0; localSiteIndex < geometry.GetSitesPerBlock();
          ++localSiteIndex)
      {
        try {
          geometry.Blocks[block].Sites.push_back(ParseSite(reader));
        } catch (Exception const& error) {
          throw Exception() << "Malformed geometry block " << block << " site "
                            << localSiteIndex << ": " << error.what();
        }
      }
    }

    GeometrySite GeometryReader::ParseSite(io::XdrReader& reader)
    {
      // Read the site type
      unsigned readSiteType = reader.read<unsigned>();
      auto siteType = SiteTypeValidator::Run(readSiteType);
      GeometrySite readInSite(siteType == gmy::SiteType::FLUID);

      // If solid, there's nothing more to do.
      if (!readInSite.isFluid)
      {
        return readInSite;
      }

      // Prepare the links array to have enough space.
      readInSite.links.resize(latticeInfo.GetNumVectors() - 1);

      bool isGmyWallSite = false;

      // For each link direction...
      for (auto&& dir: gmy::Neighbourhood)
      {
        // read the type of the intersection and create a link...
        auto intersectionType = [&]() {
          unsigned readType = reader.read<unsigned>();
          return CutTypeValidator::Run(readType);
        } ();

        GeometrySiteLink link;
        link.type = intersectionType;

        // walls have a floating-point distance to the wall...
        if (link.type == gmy::CutType::WALL)
        {
          isGmyWallSite = true;
          float distance = reader.read<float>();
          link.distanceToIntersection = distance;
        }
        // inlets and outlets (which together with none make up the other intersection types)
        // have an iolet id and a distance float...
        else if (link.type != gmy::CutType::NONE)
        {
          unsigned ioletId = reader.read<unsigned>();
          float distance = reader.read<float>();

          link.ioletId = ioletId;
          link.distanceToIntersection = distance;
        }

        // Now, attempt to match the direction read from the local neighbourhood to one in the
        // lattice being used for simulation. If a match is found, assign the link to the read
        // site.
        for (Direction usedLatticeDirection = 1; usedLatticeDirection < latticeInfo.GetNumVectors();
            usedLatticeDirection++)
        {
          if (latticeInfo.GetVector(usedLatticeDirection) == dir)
          {
            // If this link direction is necessary to the lattice in use, keep the link data.
            readInSite.links[usedLatticeDirection - 1] = link;
            break;
          }
        }
      }

      auto normalAvailable = [&]() {
        unsigned normalAvailable = reader.read<unsigned>();
        return WallNormalAvailabilityValidator::Run(normalAvailable);
      }();
      readInSite.wallNormalAvailable = (normalAvailable == gmy::WallNormalAvailability::AVAILABLE);

      if (readInSite.wallNormalAvailable != isGmyWallSite)
      {
        std::string msg = isGmyWallSite ?
          "wall fluid site without" :
          "bulk fluid site with";
        throw Exception() << "Malformed GMY file, " << msg
            << " a defined wall normal currently not allowed.";
      }

      if (readInSite.wallNormalAvailable)
      {
        readInSite.wallNormal[0] = reader.read<float>();
        readInSite.wallNormal[1] = reader.read<float>();
        readInSite.wallNormal[2] = reader.read<float>();
      }

      return readInSite;
    }

    /**
     * This function is only called if in geometry-validation mode.
     * @param geometry
     */
    void GeometryReader::ValidateGeometry(const GmyReadResult& geometry)
    {
      log::Logger::Log<log::Debug, log::OnePerCore>("Validating the GlobalLatticeData");

      auto const SPB = geometry.GetSitesPerBlock();
      auto const NV = latticeInfo.GetNumVectors();
      constexpr auto UMAX = std::numeric_limits<unsigned>::max();

      std::vector<proc_t> myProcForSite(SPB);
      std::vector<unsigned> dummySiteData(SPB * NV);

      // We check the isFluid property and the link type for each direction
      // We also validate that each processor has the same beliefs about each site.
      for (auto const& [block_gmy, count]: fluidSitesOnEachBlock) {
        auto const& block = geometry.Blocks[block_gmy];

        if (block.Sites.empty()) {
          std::fill(myProcForSite.begin(), myProcForSite.end(), SITE_OR_BLOCK_SOLID);
          std::fill(dummySiteData.begin(), dummySiteData.end(), UMAX);
        } else {
          for (site_t localSite = 0; localSite < SPB; ++localSite) {
            auto const& site = block.Sites[localSite];
            myProcForSite[localSite] = site.targetProcessor;

            auto dsd_start = localSite*NV;
            dummySiteData[dsd_start] = site.isFluid;
            for (Direction direction = 1; direction < NV; ++direction) {
              dummySiteData[dsd_start + direction] = site.isFluid ? unsigned(site.links[direction-1].type) : UMAX;
            }
          }
        }

        // Reduce using a maximum to find the actual processor for each site (ignoring the
        // invalid entries).
        std::vector<proc_t> procForSiteRecv = computeComms.AllReduce(myProcForSite, MPI_MAX);
        std::vector<unsigned> siteDataRecv = computeComms.AllReduce(dummySiteData, MPI_MIN);

        for (site_t site = 0; site < SPB; ++site) {
          if (myProcForSite[site] < 0)
            continue;

          if (myProcForSite[site] != procForSiteRecv[site]) {
            log::Logger::Log<log::Critical, log::OnePerCore>(
                "Site %li of block %li believed to be on %li but other process thinks %li",
                site, block_gmy, myProcForSite[site], procForSiteRecv[site]
            );
          }

          if (dummySiteData[site * NV] != siteDataRecv[site * NV]) {
            log::Logger::Log<log::Critical, log::OnePerCore>("Different fluid state was found for site %li on block %li. One: %li, Two: %li .",
                                                             site,
                                                             block_gmy,
                                                             dummySiteData[site*NV],
                                                             siteDataRecv[site*NV]
                                                             );
          }

          for (Direction dir = 1; dir < NV; ++dir) {
            if (dummySiteData[site * NV + dir] != siteDataRecv[site * NV+ dir]) {
              log::Logger::Log<log::Critical, log::OnePerCore>("Different link type was found for site %li, link %i on block %li. One: %li, Two: %li .",
                                                               site,
                                                               dir,
                                                               block_gmy,
                                                               dummySiteData[site*NV + dir],
                                                               siteDataRecv[site *NV + dir]);
            }
          }

        }
      }
    }


    // Go through blocks_wanted and add any 26-neighbouring blocks that are non-solid, using OCT ids.
    std::vector<U64> GeometryReader::DecideWhichBlocksToReadIncludingHalo(
        const GmyReadResult& geometry, const std::vector<U64>& blocks_wanted
    ) const {
      // Going to have to go from "compressed" octree order index
      // (idx) -> 3D grid coordinate (ijk) to compute neighbours. Get
      // set up for this.
      auto const block_dims = geometry.GetBlockDimensions();
      constexpr auto U16_MAX = std::numeric_limits<U16>::max();
      auto&& tree = geometry.block_store->GetTree();

      // Start with no blocks wanted.
      std::vector<bool> want_block_on_this_rank(nFluidBlocks, false);

      // Main loop
      for (auto block_idx: blocks_wanted) {
          // Could set this here, but will cover in loop over halo below
          // want_block_on_this_rank[block_idx] = true;

          auto block_ijk = tree.GetLeafCoords(block_idx);

          // Compute the bounds of neighbours to consider
          Vec16 lo, hi;
          for (int d = 0; d < 3; ++d) {
              // Beware the "usual arithmetic conversions"!
              U16 const v = block_ijk[d];
              lo[d] = v > 0U ? v - 1U : 0U;
              // Note, we will use inclusive hi limit below so max
              // allowed value is block_dims[d] - 1
              U16 const w = v < U16_MAX ? v + 1U : v;
              hi[d] = w < block_dims[d] ? w : block_dims[d] - 1;
          }

          // 3D loop
          for (U16 ni = lo[0]; ni <= hi[0]; ++ni)
              for (U16 nj = lo[1]; nj <= hi[1]; ++nj)
                  for (U16 nk = lo[2]; nk <= hi[2]; ++nk) {
                      auto neigh_ijk = Vec16{ni, nj, nk};
                      auto neigh_idx = tree.GetPath(neigh_ijk).leaf();
                      // Recall that the octree will return a path
                      // with "no child" for all levels where the
                      // requested node doesn't exist.
                      if (neigh_idx != octree::Level::NC)
                          want_block_on_this_rank[neigh_idx] = true;
                  }
      }

      std::vector<U64> ans;
      for (unsigned i = 0; i < nFluidBlocks; ++i) {
        if (want_block_on_this_rank[i])
          ans.push_back(i);
      }
      return ans;
    }

    void GeometryReader::OptimiseDomainDecomposition(GmyReadResult& geometry)
    {
      decomposition::OptimisedDecomposition optimiser(timings,
                                                      computeComms,
                                                      geometry,
                                                      latticeInfo);

      timings.reRead().Start();
      log::Logger::Log<log::Debug, log::OnePerCore>("Rereading blocks");
      for (auto& [id, block]: geometry.Blocks)
          for (auto& site: block.Sites)
              if (site.isFluid) site.targetProcessor = UNKNOWN_PROCESS;
      // Fetch only blocks newly required by the ParMETIS decomposition.
      RereadBlocks(geometry,
                   optimiser.GetStaying(),
                   optimiser.GetArriving());
      timings.reRead().Stop();

      timings.moves().Start();
      // Implement the decomposition now that we have read the necessary data.
      log::Logger::Log<log::Debug, log::OnePerCore>("Implementing moves");
      ImplementMoves(geometry,
                     optimiser.GetStaying(),
                     optimiser.GetArriving(),
                     optimiser.GetLeaving());
      timings.moves().Stop();
    }

    // The header section of the config file contains a number of records.
    site_t GeometryReader::GetHeaderLength(site_t blockCount) const
    {
      return headerRecordLength * blockCount;
    }

    // Iterator to advance through the moves vector to the first move
    // in the next block, recalling that moves are sorted by block and
    // there's a max number of sites per block.
    struct only_block_id_iterator {
        using Iter = SiteVec::const_iterator;

        site_t max_step;
        Iter pos;
        Iter end;

        // Only care about block ID
        auto operator*() const {
          return (*pos)[0];
        }

        only_block_id_iterator& operator++() {
            auto block = **this;
            ++pos; // Guard the case where have a fully fluid block
            auto limit = pos + std::min<site_t>(max_step, end - pos);
            pos = std::upper_bound(
                pos, limit,
                block,
                [](U64 l, SiteDesc const& r) {return l < r[0]; }
            );
            return *this;
        }

        operator bool() const {
            return pos < end;
        }
    };

    void GeometryReader::RereadBlocks(GmyReadResult& geometry, SiteVec const& staying,
                                      MovesMap const& arriving)
    {
      // Go through the sites that we will end up with, and compute
      // the unique list of blocks (by OCT index) that we need. Recall
      // that the moves lists are sorted by block and then site id
      // (tho we only care about block).
      std::vector<U64> optimal_blocks;

      // We can be simple for the staying blocks as opt is empty at
      // start and know:
      // - that the blocks monotonically increase thru the array
      // - that there are at most SPB sites per block
      auto const SPB = geometry.GetSitesPerBlock();
      for (auto it = only_block_id_iterator{SPB, staying.begin(), staying.end()}; it; ++it) {
          optimal_blocks.push_back(*it);
      }

      // For the arriving sites, need to deal with insertion
      for (auto& [src_rank, data]: arriving) {
        // Recall that each rank's data is sorted by block ID, thus we
        // can search for the insert point more efficiently.
        auto opt_pos = optimal_blocks.begin();

        for (auto it = only_block_id_iterator{SPB, data.begin(), data.end()}; it; ++it) {
          auto block = *it;

          auto lb = std::lower_bound(opt_pos, optimal_blocks.end(), block);
          // BUT adding a block may invalidate the opt_pos iterator
          if (lb ==  optimal_blocks.end()) {
            // Not found and greater than any value
            optimal_blocks.push_back(block);
            opt_pos = optimal_blocks.end();
          } else if (*lb == block) {
            // already there
            ++opt_pos;
          } else {
            // Not found and need to insert and move past the inserted one
            opt_pos = ++optimal_blocks.insert(lb, block);
          }
        }
      }

      // Reread the blocks into the GlobalLatticeData now.
      ReadInBlocksWithHalo(geometry, optimal_blocks);
    }

    void GeometryReader::ImplementMoves(GmyReadResult& geometry,
                                        SiteVec const& staying,
                                        MovesMap const& arriving,
                                        MovesMap const& leaving) const
    {
        // Given a vector of sites (sorted by block, then site index
        // within the block), assign the process to that site in the
        // GmyReadResult.
        auto set_rank_for_sites = [&] (SiteVec const& sites, int rank) {

            auto block_start = sites.begin();
            auto end = sites.end();
            auto&& tree = geometry.block_store->GetTree();

            while (block_start != end) {
                auto const block_oct = (*block_start)[0];
                site_t const nsites = tree.levels[tree.n_levels].sites_per_node[block_oct];
                auto const block_end = (++only_block_id_iterator{nsites, block_start, end}).pos;

                auto const block_ijk = tree.GetLeafCoords(block_oct);
                auto const block_gmy = geometry.GetBlockIdFromBlockCoordinates(block_ijk);

                auto& block_sites = geometry.Blocks[block_gmy].Sites;
                for (auto it = block_start; it < block_end; ++it) {
                    auto [block, site_idx] = *it;
                    HASSERT(block == block_oct);
                    HASSERT(!block_sites.empty());

                    auto& site = block_sites[site_idx];
                    HASSERT(site.isFluid);
                    site.targetProcessor = rank;
                }

                block_start = block_end;
            }
        };

        // First set proc for staying sites
        set_rank_for_sites(staying, computeComms.Rank());
        // Then arriving sites
        for (auto const& [_, sites]: arriving) {
          set_rank_for_sites(sites, computeComms.Rank());
        }
        // We could set the leaving sites, but can't guarantee we get
        // all the neighbours of our sites, so no saving of time
        // really.
    }

}
