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
      offsetPath = fmt::offset::ExtractionToOffset(filePath.string());
    }
  }

  void LocalDistributionInput::LoadDistribution(geometry::FieldData* latDat, std::optional<LatticeTimeStep>& targetTime)
  {
      auto&& dom = latDat->GetDomain();
      const auto NUMVECTORS = dom.GetLatticeInfo().GetNumVectors();
      auto inputFile = net::MpiFile::Open(comms, filePath, MPI_MODE_RDONLY);
      inputFile.SetView(0, MPI_CHAR, MPI_CHAR, "native");
      // Keep the collective file alive while sharing root-only parse errors.
      // Throwing only on rank zero would block its collective close while
      // other ranks wait for metadata broadcasts.
      std::string metadataError;
      try {
          ReadExtractionHeaders(inputFile, NUMVECTORS);
          ReadOffsets(offsetPath.string());
      } catch (std::exception const& error) {
          metadataError = error.what();
      }
      comms.Broadcast(metadataError, comms.GetIORank());
      if (!metadataError.empty()) throw Exception() << metadataError;
      comms.Broadcast(distributionBytes, comms.GetIORank());
      comms.Broadcast(distributionOffset, comms.GetIORank());
      comms.Broadcast(dataStart, comms.GetIORank());
      comms.Broadcast(checkpointSiteCount, comms.GetIORank());
      comms.Broadcast(allCoresWriteLength, comms.GetIORank());
      const uint64_t siteLength = 3 * sizeof(uint32_t) + NUMVECTORS * distributionBytes;
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
        std::vector<std::byte> buffer(8);
        inputFile.ReadAt(dataStart + index * allCoresWriteLength, to_span(buffer));
        io::XdrMemReader reader(buffer);
        uint64_t value;
        reader.read(value);
        return value;
      };
      uint64_t iTS = 0;
      std::string timeError;
      try {
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
      } catch (std::exception const& error) {
        timeError = error.what();
      }
      comms.Broadcast(timeError, comms.GetIORank());
      if (!timeError.empty()) throw Exception() << timeError;
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
        std::vector<std::byte> dataBuffer(count * siteLength);
        if (count)
          inputFile.ReadAt(dataStart + iTS * allCoresWriteLength + 8 + begin * siteLength,
                           to_span(dataBuffer));

        std::vector<std::vector<std::byte>> outgoing(comms.Size());
        for (uint64_t site = 0; site < count; ++site) {
          const std::byte* record = dataBuffer.data() + site * siteLength;
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
        std::vector<std::byte> sendBuffer;
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
        std::vector<std::byte> recvBuffer(recvTotal);
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
            oldValues[i] = (distributionBytes == sizeof(float) ? double(reader.read<float>()) : reader.read<double>())
                          + distributionOffset;
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
        if (!comms.OnIORank()) return;
        std::vector<std::byte> magic(12);
        inputFile.Read(to_span(magic));
        io::XdrMemReader magicReader(magic);
        const auto hlbMagic = magicReader.read<uint32_t>();
        const auto xtrMagic = magicReader.read<uint32_t>();
        const auto version = magicReader.read<uint32_t>();
        if (hlbMagic != fmt::HemeLbMagicNumber || xtrMagic != fmt::extraction::MagicNumber)
            throw Exception() << "Invalid checkpoint extraction magic";
        if (version != 4 && version != 5 && version != 6)
            throw Exception() << "Unsupported checkpoint extraction version: " << version;
        const uint64_t mainHeaderLength = version == 6 ? 84 : 60;
        std::vector<std::byte> main(mainHeaderLength - magic.size());
        inputFile.Read(to_span(main));
        io::XdrMemReader header(main);
        const auto dx = header.read<double>();
        if (version == 6) { header.read<double>(); header.read<double>(); }
        PhysicalPosition origin;
        for (int i = 0; i < 3; ++i) header.read(origin[i]);
        if (version == 6) header.read<double>();
        if (expectedVoxelSize) {
            const auto tol = 1e-9 * std::abs(*expectedVoxelSize);
            if (!std::isfinite(dx) || std::abs(dx - *expectedVoxelSize) > tol)
                throw Exception() << "Checkpoint was written with voxel size " << dx
                                  << " m but this run uses " << *expectedVoxelSize << " m";
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(origin[i]) || std::abs(origin[i] - expectedOrigin[i]) > tol)
                    throw Exception() << "Checkpoint was written with origin " << origin
                                      << " m but this run uses " << expectedOrigin << " m";
        }
        checkpointSiteCount = header.read<uint64_t>();
        const auto fields = header.read<uint32_t>();
        const auto fieldLength = header.read<uint32_t>();
        if (fields != 1) throw Exception() << "Checkpoint file must contain exactly one distributions field";
        if (fieldLength < 32 || fieldLength > 48)
            throw Exception() << "Invalid checkpoint field header length: " << fieldLength;
        std::vector<std::byte> fieldBuffer(fieldLength);
        inputFile.Read(to_span(fieldBuffer));
        io::XdrMemReader field(fieldBuffer);
        field.read(distField.name);
        field.read(distField.numberOfElements);
        if (distField.name != "distributions") throw Exception() << "Checkpoint field must be named distributions";
        if (distField.numberOfElements != NUMVECTORS)
            throw Exception() << "Checkpoint field has " << distField.numberOfElements
                              << " distributions but this build requires " << NUMVECTORS;
        distributionOffset = 0.0;
        if (version == 4) {
            distributionBytes = sizeof(float);
            distributionOffset = field.read<double>();
        } else {
            field.read(distField.typecode);
            const auto tc = static_cast<fmt::extraction::TypeCode>(distField.typecode);
            if (tc != fmt::extraction::TypeCode::FLOAT && tc != fmt::extraction::TypeCode::DOUBLE)
                throw Exception() << "Checkpoint distributions must be float or double";
            distributionBytes = tc == fmt::extraction::TypeCode::FLOAT ? sizeof(float) : sizeof(double);
            field.read(distField.numberOfOffsets);
            if (distField.numberOfOffsets > 1) throw Exception() << "Invalid checkpoint distribution offsets";
            if (distField.numberOfOffsets == 1)
                distributionOffset = distributionBytes == sizeof(float) ? double(field.read<float>()) : field.read<double>();
            if (version == 6) {
                const double scale = distributionBytes == sizeof(float) ? double(field.read<float>()) : field.read<double>();
                if (scale != 0.0) throw Exception() << "Checkpoint has scaling applied";
            }
        }
        if (!std::isfinite(distributionOffset)) throw Exception() << "Non-finite checkpoint distribution offset";
        if (field.GetPosition() != fieldLength) throw Exception() << "Checkpoint field header has trailing bytes";
        headerLength = mainHeaderLength + fieldLength;
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
	if (dataStart != headerLength)
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
    }
}
