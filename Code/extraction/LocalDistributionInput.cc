// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.
#include "extraction/LocalDistributionInput.h"

#include "extraction/OutputField.h"
#include "geometry/FieldData.h"
#include "io/formats/formats.h"
#include "io/formats/extraction.h"
#include "io/formats/offset.h"
#include "io/readers/XdrFileReader.h"
#include "io/readers/XdrMemReader.h"
#include "log/Logger.h"
#include "util/span.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>

namespace hemelb::extraction {
  namespace fmt = hemelb::io::formats;

  // The reader expects each checkpoint record to start with the 8-byte
  // timestep, which LocalPropertyOutput writes from the I/O rank before its
  // own sites. That puts it first only if the I/O rank is rank 0.
  static_assert(net::IOCommunicator::IO_RANK == 0,
                "Checkpoint records assume the I/O rank writes first");

  void LocalDistributionInput::ExpectGeometry(PhysicalDistance voxelSize, PhysicalPosition const& origin) {
    expectedVoxelSize = voxelSize;
    expectedOrigin = origin;
  }

  LocalDistributionInput::LocalDistributionInput(std::filesystem::path dataFilePath,
						 std::optional<std::filesystem::path> maybeOffsetPath,
						 const net::IOCommunicator& ioComm) :
    comms{ioComm}, filePath{std::move(dataFilePath)}
  {
    if (maybeOffsetPath) {
      offsetPath = std::move(*maybeOffsetPath);
    } else {
      offsetPath = fmt::offset::ExtractionToOffset(filePath);
    }
  }

  namespace {
    // The required xtr field header len
    uint64_t constexpr expectedFieldHeaderLength = 32U;
    uint64_t constexpr totalXtrHeaderLength = fmt::extraction::MainHeaderLength + expectedFieldHeaderLength;
  }

  void LocalDistributionInput::LoadDistribution(geometry::FieldData* latDat, std::optional<LatticeTimeStep>& targetTime)
  {
      auto&& dom = latDat->GetDomain();
      const auto NUMVECTORS = dom.GetLatticeInfo().GetNumVectors();
      auto inputFile = net::MpiFile::Open(comms, filePath, MPI_MODE_RDONLY);
      inputFile.SetView(0, MPI_CHAR, MPI_CHAR, "native");
      ReadExtractionHeaders(inputFile, NUMVECTORS);
      ReadOffsets(offsetPath);
      const uint64_t siteLength = 3 * sizeof(uint32_t) + NUMVECTORS * sizeof(double);
      if (allCoresWriteLength < 8 || (allCoresWriteLength - 8) % siteLength != 0 ||
          checkpointSiteCount != (allCoresWriteLength - 8) / siteLength)
        throw Exception() << "Checkpoint site count or record length is inconsistent with its offsets";
      const auto currentSiteCount = comms.AllReduce(uint64_t(dom.GetLocalFluidSiteCount()), MPI_SUM);
      if (currentSiteCount != checkpointSiteCount)
        throw Exception() << "Checkpoint has " << checkpointSiteCount
                          << " sites but current geometry has " << currentSiteCount;

      const uint64_t fileSize = inputFile.GetSize();
      if (fileSize < dataStart || allCoresWriteLength == 0 ||
          (fileSize - dataStart) % allCoresWriteLength != 0)
        throw Exception() << "Checkpoint file length is inconsistent with its offsets";
      const uint64_t nTimes = (fileSize - dataStart) / allCoresWriteLength;
      if (nTimes == 0)
        throw Exception() << "Checkpoint file contains no timesteps";

      auto readTime = [&](uint64_t index) {
        std::vector<char> buffer(8);
        inputFile.ReadAt(dataStart + index * allCoresWriteLength, to_span(buffer));
        io::XdrMemReader reader(buffer);
        uint64_t value;
        reader.read(value);
        return value;
      };
      uint64_t iTS = 0;
      if (comms.OnIORank()) {
        if (targetTime) {
          uint64_t low = 0, high = nTimes;
          while (low < high) {
            const auto mid = low + (high - low) / 2;
            if (readTime(mid) < *targetTime) low = mid + 1;
            else high = mid;
          }
          iTS = low;
          if (iTS == nTimes || readTime(iTS) != *targetTime)
            throw Exception() << "Target timestep " << *targetTime << " not found in checkpoint file";
        } else {
          iTS = nTimes - 1;
        }
        timestep = readTime(iTS);
      }
      comms.Broadcast(iTS, comms.GetIORank());
      comms.Broadcast(timestep, comms.GetIORank());
      targetTime = timestep;
      log::Logger::Log<log::Info, log::Singleton>("Reading checkpoint from timestep %d with index %d", timestep, iTS);

      // Each current rank reads a contiguous share of saved sites, irrespective
      // of how many ranks wrote the checkpoint. Exchanges use bounded batches.
      const auto siteAtRank = [&](uint64_t rank) {
        return (checkpointSiteCount / comms.Size()) * rank +
               (checkpointSiteCount % comms.Size()) * rank / comms.Size();
      };
      const uint64_t firstSite = siteAtRank(comms.Rank());
      const uint64_t lastSite = siteAtRank(comms.Rank() + 1);
      const uint64_t batchSites = std::max<uint64_t>(1, std::min<uint64_t>(8192,
                                  std::min<uint64_t>(INT_MAX, 64 * 1024 * 1024) /
                                  (siteLength * comms.Size())));
      const uint64_t largestShare = siteAtRank(comms.Size()) - siteAtRank(comms.Size() - 1);
      const uint64_t rounds = largestShare / batchSites + (largestShare % batchSites != 0);
      std::vector<unsigned char> seen(dom.GetLocalFluidSiteCount(), 0);
      int invalidSite = 0;
      for (uint64_t round = 0; round < rounds; ++round) {
        const uint64_t begin = std::min(lastSite, firstSite + round * batchSites);
        const uint64_t count = std::min(batchSites, lastSite - begin);
        std::vector<char> dataBuffer(count * siteLength);
        if (count)
          inputFile.ReadAt(dataStart + iTS * allCoresWriteLength + 8 + begin * siteLength,
                           to_span(dataBuffer));

        std::vector<std::vector<char>> outgoing(comms.Size());
        for (uint64_t site = 0; site < count; ++site) {
          const char* record = dataBuffer.data() + site * siteLength;
          io::XdrMemReader reader(record, 3 * sizeof(uint32_t));
          util::Vector3D<uint32_t> coords;
          reader.read(coords.x());
          reader.read(coords.y());
          reader.read(coords.z());
          util::Vector3D<site_t> grid{coords};
          if (!dom.IsValidLatticeSite(grid)) { invalidSite = 1; continue; }
          const proc_t owner = dom.GetProcIdFromGlobalCoords(grid);
          if (owner < 0 || owner >= comms.Size()) { invalidSite = 1; continue; }
          outgoing[owner].insert(outgoing[owner].end(), record, record + siteLength);
        }
        if (comms.AllReduce(invalidSite, MPI_MAX))
          throw Exception() << "Checkpoint contains a site outside the current fluid geometry";

        std::vector<int> sendCounts(comms.Size()), sendOffsets(comms.Size());
        int sendTotal = 0;
        for (int rank = 0; rank < comms.Size(); ++rank) {
          sendOffsets[rank] = sendTotal;
          sendCounts[rank] = int(outgoing[rank].size());
          sendTotal += sendCounts[rank];
        }
        std::vector<char> sendBuffer;
        sendBuffer.reserve(sendTotal);
        for (auto& portion : outgoing)
          sendBuffer.insert(sendBuffer.end(), portion.begin(), portion.end());
        auto recvCounts = comms.AllToAll(sendCounts);
        std::vector<int> recvOffsets(comms.Size());
        int recvTotal = 0;
        for (int rank = 0; rank < comms.Size(); ++rank) {
          recvOffsets[rank] = recvTotal;
          recvTotal += recvCounts[rank];
        }
        std::vector<char> recvBuffer(recvTotal);
        net::MpiCall{MPI_Alltoallv}(sendBuffer.data(), sendCounts.data(), sendOffsets.data(), MPI_BYTE,
                                   recvBuffer.data(), recvCounts.data(), recvOffsets.data(), MPI_BYTE, comms);

        for (uint64_t offset = 0; offset < recvBuffer.size(); offset += siteLength) {
          io::XdrMemReader reader(recvBuffer.data() + offset, siteLength);
          util::Vector3D<uint32_t> coords;
          reader.read(coords.x());
          reader.read(coords.y());
          reader.read(coords.z());
          util::Vector3D<site_t> grid{coords};
          proc_t owner;
          site_t index;
          if (!dom.GetContiguousSiteId(grid, owner, index) || owner != comms.Rank() ||
              index < 0 || index >= dom.GetLocalFluidSiteCount() || seen[index]) {
            invalidSite = 1;
            continue;
          }
          seen[index] = 1;
          distribn_t* oldValues = latDat->GetFOld(index * NUMVECTORS);
          distribn_t* newValues = latDat->GetFNew(index * NUMVECTORS);
          for (unsigned i = 0; i < NUMVECTORS; ++i)
            reader.read(oldValues[i]);
          std::copy(oldValues, oldValues + NUMVECTORS, newValues);
        }
        if (comms.AllReduce(invalidSite, MPI_MAX))
          throw Exception() << "Checkpoint has an invalid or duplicate site";
      }
      const int missingSite = std::find(seen.begin(), seen.end(), 0) != seen.end();
      if (comms.AllReduce(missingSite, MPI_MAX))
        throw Exception() << "Checkpoint is missing sites from the current geometry";
    }

    void LocalDistributionInput::ReadExtractionHeaders(net::MpiFile& inputFile, const unsigned NUMVECTORS) {
      // The headers technically aren't needed (because of the offset
      // file), but we check that they are as expected.
      if (comms.OnIORank()) {
	auto preambleBuf = std::vector<char>(fmt::extraction::MainHeaderLength);
	inputFile.Read(to_span(preambleBuf));
	auto preambleReader = io::XdrMemReader(preambleBuf);

	// Read the magic numbers.
	uint32_t hlbMagicNumber, extMagicNumber, version;
	preambleReader.read(hlbMagicNumber);
	preambleReader.read(extMagicNumber);
	preambleReader.read(version);

	// Check the value of the HemeLB magic number.
	if (hlbMagicNumber != fmt::HemeLbMagicNumber)
	{
	  throw Exception() << "This file does not start with the HemeLB magic number."
			    << " Expected: " << unsigned(fmt::HemeLbMagicNumber)
			    << " Actual: " << hlbMagicNumber;
	}

	// Check the value of the extraction file magic number.
	if (extMagicNumber != fmt::extraction::MagicNumber)
        {
	  throw Exception() << "This file does not have the extraction magic number."
			    << " Expected: " << unsigned(fmt::extraction::MagicNumber)
			    << " Actual: " << extMagicNumber;
	}

	// Check the version number.
	if (version != fmt::extraction::VersionNumber)
	{
	  throw Exception() << "Version number incorrect."
			    << " Supported: " << unsigned(fmt::extraction::VersionNumber)
			    << " Input: " << version;
	}

	{
	  // Obtain the size of voxel in metres.
	  double voxelSize;
	  preambleReader.read(voxelSize);

	  // Obtain the origin.
	  double origin[3];
	  preambleReader.read(origin[0]);
	  preambleReader.read(origin[1]);
	  preambleReader.read(origin[2]);

	  if (expectedVoxelSize) {
	    // Written from the same double values, so equal up to rounding.
	    auto const tol = 1e-9 * std::abs(*expectedVoxelSize);
	    auto const close = [tol](double a, double b) { return std::abs(a - b) <= tol; };
	    if (!close(voxelSize, *expectedVoxelSize))
	      throw Exception() << "Checkpoint was written with voxel size " << voxelSize
				<< " m but this run uses " << *expectedVoxelSize << " m";
	    for (int i = 0; i < 3; ++i)
	      if (!close(origin[i], expectedOrigin[i]))
		throw Exception() << "Checkpoint was written with origin (" << origin[0] << ", "
				  << origin[1] << ", " << origin[2] << ") m but this run uses ("
				  << expectedOrigin[0] << ", " << expectedOrigin[1] << ", "
				  << expectedOrigin[2] << ") m";
	  }
	}
	// Obtain the total number of sites, fields & header len
	uint64_t numberOfSites;
	uint32_t numberOfFields, lengthOfFieldHeader;
	preambleReader.read(numberOfSites);
	preambleReader.read(numberOfFields);
	preambleReader.read(lengthOfFieldHeader);
	checkpointSiteCount = numberOfSites;

	if (numberOfFields != 1 )
	  throw Exception() << "Checkpoint file must contain exactly one field, the distributions, but has "
			    << numberOfFields;
	if (lengthOfFieldHeader != expectedFieldHeaderLength)
	  throw Exception() << "Checkpoint file's field header must be "
			    << expectedFieldHeaderLength << " B long, but is "
			    << lengthOfFieldHeader << " B";

	auto fieldHeaderBuf = std::vector<char>(lengthOfFieldHeader);
	inputFile.Read(to_span(fieldHeaderBuf));
	auto fieldHeaderReader = io::XdrMemReader(fieldHeaderBuf);

	fieldHeaderReader.read(distField.name);
	fieldHeaderReader.read(distField.numberOfElements);
	fieldHeaderReader.read(distField.typecode);
	fieldHeaderReader.read(distField.numberOfOffsets);

	if (distField.name != "distributions")
	  throw Exception() << "Checkpoint file must contain field named 'distributions', but has '"
			    << distField.name << "'";

	if (distField.numberOfElements != NUMVECTORS)
	  throw Exception() << "Checkpoint field distributions contains " << distField.numberOfElements
			    << " distributions but this build of HemeLB requires " << NUMVECTORS;

	if (distField.typecode != static_cast<std::uint32_t>(io::formats::extraction::TypeCode::DOUBLE))
	  throw Exception() << "Checkpoint contains wrong data type";

	if (distField.numberOfOffsets != 0)
	  throw Exception() << "Checkpoint should not have offsets";

      }
    }

    void LocalDistributionInput::ReadOffsets(const std::string& offsetFileName) {
      // Only actually read on IO rank
      if (comms.OnIORank()) {
	io::XdrFileReader offsetReader(offsetFileName);
	uint32_t hlbMagicNumber, offMagicNumber, version;
	int32_t nRanks;
	offsetReader.read(hlbMagicNumber);
	offsetReader.read(offMagicNumber);
	offsetReader.read(version);
	offsetReader.read(nRanks);

	if (hlbMagicNumber != fmt::HemeLbMagicNumber)
	  throw Exception() << "This file does not start with the HemeLB magic number."
			    << " Expected: " << unsigned(fmt::HemeLbMagicNumber)
			    << " Actual: " << hlbMagicNumber;

	if (offMagicNumber != fmt::offset::MagicNumber)
	  throw Exception() << "This file does not have the offset magic number."
			    << " Expected: " << unsigned(fmt::offset::MagicNumber)
			    << " Actual: " << offMagicNumber;

	if (version != fmt::offset::VersionNumber)
	  throw Exception() << "Version number incorrect."
			    << " Supported: " << unsigned(fmt::offset::VersionNumber)
			    << " Input: " << version;

	if (nRanks < 1)
	  throw Exception() << "Offset file has no MPI ranks";
	uint64_t previous;
	offsetReader.read(dataStart);
	if (dataStart != totalXtrHeaderLength)
	  throw Exception() << "Offset file starts at an unexpected position";
	previous = dataStart;
	for (int i = 0; i < nRanks; ++i) {
	  uint64_t next;
	  offsetReader.read(next);
	  if (next < previous)
	    throw Exception() << "Offset file positions are not increasing";
	  previous = next;
	}
	allCoresWriteLength = previous - dataStart;
      }
      comms.Broadcast(dataStart, comms.GetIORank());
      comms.Broadcast(checkpointSiteCount, comms.GetIORank());
      comms.Broadcast(allCoresWriteLength, comms.GetIORank());
    }
}
