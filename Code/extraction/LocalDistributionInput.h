// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_EXTRACTION_LOCALDISTRIBUTIONINPUT_H
#define HEMELB_EXTRACTION_LOCALDISTRIBUTIONINPUT_H

#include <optional>
#include <filesystem>

#include "extraction/IterableDataSource.h"
#include "extraction/InputField.h"
#include "io/readers/XdrMemReader.h"
#include "lb/Lattices.h"
#include "net/mpi.h"
#include "net/MpiFile.h"
#include "net/IOCommunicator.h"


namespace hemelb
{
  namespace net
  {
    class IOCommunicator;
  }
  namespace geometry
  {
    class FieldData;
  }
  namespace extraction
  {
    // Read a checkpoint and redistribute its sites to the current domain.
    class LocalDistributionInput
    {
      public:
      // Construct, but don't do any I/O.
      //
      // Take the path string by value since we will move it into a member
      // anyway.
      LocalDistributionInput(std::filesystem::path dataFilePath, std::optional<std::filesystem::path> maybeOffsetPath, const net::IOCommunicator& ioComms);

      // Open the file and load our part into the domain_type
      // instance.
      //
      // Time is optional, if not supplied will use the last one in
      // the file and will set the argument to that value.
      //
      void LoadDistribution(geometry::FieldData* latDat, std::optional<LatticeTimeStep>& initalTime);

      // Require the checkpoint to have been written with this voxel size and
      // origin (metres). Without it, only the site count and coordinates are
      // checked, so a checkpoint from a differently scaled or shifted run
      // would load silently.
      void ExpectGeometry(PhysicalDistance voxelSize, PhysicalPosition const& origin);

    private:

      void ReadExtractionHeaders(net::MpiFile&, const unsigned NUMVECTORS);
      void ReadOffsets(const std::string&);

      const net::IOCommunicator& comms;

      // The path to the file to read from.
      std::filesystem::path filePath;
      std::filesystem::path offsetPath;

      InputField distField;
      uint64_t dataStart;
      uint64_t headerLength = 0;
      uint64_t distributionBytes = sizeof(double);
      double distributionOffset = 0.0;
      uint64_t checkpointSiteCount;
      uint64_t timestep;
      uint64_t allCoresWriteLength;
      std::optional<PhysicalDistance> expectedVoxelSize;
      PhysicalPosition expectedOrigin;
    };
  }
}

#endif // HEMELB_EXTRACTION_LOCALDISTRIBUTIONINPUT_H
