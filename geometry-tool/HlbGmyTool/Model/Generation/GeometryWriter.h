// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELBSETUPTOOL_GEOMETRYWRITER_H
#define HEMELBSETUPTOOL_GEOMETRYWRITER_H

#include <cstdio>
#include <string>

#include "Index.h"

#include "io/writers/XdrWriter.h"
using hemelb::io::XdrWriter;

class BlockWriter;
class BufferPool;

class GeometryWriter {
 public:
  GeometryWriter(const std::string& OutputGeometryFile,
                 int BlockSize,
                 Index BlockCounts);

  ~GeometryWriter();

  void Close();
  BlockWriter* StartNextBlock();

 protected:
  std::string OutputGeometryFile;
  int BlockSize;
  Index BlockCounts;

  int headerStart = 0;
  XdrWriter* headerEncoder = nullptr;
  unsigned int headerBufferLength = 0;
  char* headerBuffer = nullptr;

  int bodyStart = 0;
  FILE* bodyFile = nullptr;
  BufferPool* BlockBufferPool = nullptr;
  friend class BlockWriter;
};

#endif  // HEMELBSETUPTOOL_GEOMETRYWRITER_H
