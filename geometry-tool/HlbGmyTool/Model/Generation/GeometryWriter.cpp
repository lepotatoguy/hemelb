// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "GeometryWriter.h"
#include "BlockWriter.h"
#include "BufferPool.h"
#include "GenerationError.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

#include "io/formats/formats.h"
#include "io/formats/geometry.h"
#include "io/writers/XdrMemWriter.h"

using hemelb::io::formats::geometry;

namespace {
unsigned int ValidateGeometrySize(int blockSize, const Index& blockCounts) {
  if (blockSize <= 0 || blockSize > std::numeric_limits<std::uint16_t>::max())
    throw GenerationErrorMessage("Geometry block size is outside the supported range");

  std::uint64_t nBlocks = 1;
  for (unsigned int i = 0; i < 3; ++i) {
    if (blockCounts[i] <= 0 || blockCounts[i] > std::numeric_limits<std::uint16_t>::max())
      throw GenerationErrorMessage("Geometry block count is outside the supported range");
    nBlocks *= static_cast<std::uint64_t>(blockCounts[i]);
  }
  const auto headerBytes = nBlocks * geometry::HeaderRecordLength;
  if (headerBytes > std::numeric_limits<int>::max() - geometry::PreambleLength)
    throw GenerationErrorMessage("Geometry header is too large for the file writer");

  const auto sitesPerBlock = std::uint64_t(blockSize) * blockSize * blockSize;
  const auto maxBlockBytes = sitesPerBlock * geometry::MaxFluidSiteRecordLength;
  if (maxBlockBytes > std::numeric_limits<unsigned int>::max())
    throw GenerationErrorMessage("Geometry block is too large for the file writer");
  return static_cast<unsigned int>(maxBlockBytes);
}
}

GeometryWriter::GeometryWriter(const std::string& OutputGeometryFile,
                               int BlockSize,
                               Index BlockCounts)
    : OutputGeometryFile(OutputGeometryFile), BlockSize(BlockSize) {
  const auto maxBlockBytes = ValidateGeometrySize(BlockSize, BlockCounts);
  auto blockBufferPool = std::make_unique<BufferPool>(maxBlockBytes);

  for (unsigned int i = 0; i < 3; ++i) {
    this->BlockCounts[i] = BlockCounts[i];
  }

  std::array<char, geometry::PreambleLength> preamble{};
  {
    hemelb::io::XdrMemWriter encoder(preamble.data(), preamble.size());
    // General magic
    encoder << static_cast<unsigned int>(
        hemelb::io::formats::HemeLbMagicNumber);
    // Geometry magic
    encoder << static_cast<unsigned int>(
        hemelb::io::formats::geometry::MagicNumber);
    // Geometry file format version number
    encoder << static_cast<unsigned int>(
        hemelb::io::formats::geometry::VersionNumber);

    // Blocks in each dimension
    for (unsigned int i = 0; i < 3; ++i)
      encoder << this->BlockCounts[i];

    // Sites along 1 dimension of a block
    encoder << this->BlockSize;

    // padding
    encoder << 0U;
    if (encoder.getCurrentStreamPosition() != preamble.size())
      throw GenerationErrorMessage("Geometry preamble has an unexpected length");
  }

  this->headerStart = geometry::PreambleLength;
  const auto nBlocks = std::uint64_t(this->BlockCounts[0]) * this->BlockCounts[1] * this->BlockCounts[2];
  this->headerBufferLength = static_cast<unsigned int>(nBlocks * geometry::HeaderRecordLength);
  this->bodyStart = this->headerStart + this->headerBufferLength;
  auto headerBuffer = std::make_unique<char[]>(this->headerBufferLength);
  auto headerEncoder = std::make_unique<hemelb::io::XdrMemWriter>(
      headerBuffer.get(), this->headerBufferLength);

  std::FILE* headerFile = std::fopen(this->OutputGeometryFile.c_str(), "wb");
  if (!headerFile)
    throw GenerationErrorMessage("Failed to create geometry file: " + this->OutputGeometryFile);
  const bool written = std::fwrite(preamble.data(), 1, preamble.size(), headerFile) == preamble.size() &&
                       std::fwrite(headerBuffer.get(), 1, this->headerBufferLength, headerFile) ==
                           this->headerBufferLength;
  const bool closed = std::fclose(headerFile) == 0;
  if (!written || !closed)
    throw GenerationErrorMessage("Failed to write geometry preamble and header: " + this->OutputGeometryFile);

  this->bodyFile = std::fopen(this->OutputGeometryFile.c_str(), "ab");
  if (!this->bodyFile)
    throw GenerationErrorMessage("Failed to reopen geometry file for block data: " + this->OutputGeometryFile);
  this->BlockBufferPool = blockBufferPool.release();
  this->headerBuffer = headerBuffer.release();
  this->headerEncoder = headerEncoder.release();
}

GeometryWriter::~GeometryWriter() {
  delete this->headerEncoder;
  delete[] this->headerBuffer;
  // Check this is still here as Close() will delete this
  if (this->bodyFile != NULL)
    std::fclose(this->bodyFile);
  delete this->BlockBufferPool;
}

void GeometryWriter::Close() {
  // Close the geometry file
  if (std::fclose(this->bodyFile) != 0) {
    this->bodyFile = nullptr;
    throw GenerationErrorMessage("Failed to close geometry block data: " + this->OutputGeometryFile);
  }
  this->bodyFile = NULL;

  // Reopen it, write the header buffer, close it.
  std::FILE* cfg = std::fopen(this->OutputGeometryFile.c_str(), "rb+");
  if (!cfg)
    throw GenerationErrorMessage("Failed to reopen geometry file for header: " + this->OutputGeometryFile);
  const bool written = std::fseek(cfg, this->headerStart, SEEK_SET) == 0 &&
                       std::fwrite(this->headerBuffer, 1, this->headerBufferLength, cfg) == this->headerBufferLength;
  const bool closed = std::fclose(cfg) == 0;
  if (!written || !closed)
    throw GenerationErrorMessage("Failed to finish geometry header: " + this->OutputGeometryFile);
}

BlockWriter* GeometryWriter::StartNextBlock() {
  return new BlockWriter(this->BlockBufferPool);
}
