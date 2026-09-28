// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "io/readers/XdrMemReader.h"

namespace hemelb::io
{
  // Constructor to create an Xdr object based on a memory buffer
  XdrMemReader::XdrMemReader(const char* buf, unsigned int dataLength)
    : start(buf), len(dataLength)
  {
  }

  XdrMemReader::XdrMemReader(const std::vector<char>& dataVec)
    : start(dataVec.data()), len(dataVec.size())
  {
  }

  unsigned XdrMemReader::GetPosition() {
    return position;
  }

  const char* XdrMemReader::get_bytes(size_t n) {
    if (n > len - position)
      throw Exception() << "Truncated XDR data at byte " << position
                        << ": need " << n << " more bytes, " << len - position << " available";
    if (n == 0)
      return start ? start : "";
    if (!start)
      throw Exception() << "XDR data buffer is null";
    auto ans = start + position;
    position += n;
    return ans;
  }

}
