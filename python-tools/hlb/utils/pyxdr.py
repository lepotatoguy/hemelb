# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

"""Pure Python XDR (RFC 4506) packing and unpacking.

A replacement for the parts of the standard library's ``xdrlib`` that
HemeLB uses; ``xdrlib`` was removed in Python 3.13. Method names, results
and exceptions match ``xdrlib``. ``tests/test_pyxdr.py`` compares the two
wherever ``xdrlib`` is still available.
"""

import struct
from io import BytesIO


class Error(Exception):
    """Base class for XDR errors."""

    def __init__(self, msg):
        self.msg = msg

    def __repr__(self):
        return repr(self.msg)

    def __str__(self):
        return str(self.msg)


class ConversionError(Error):
    """A value could not be packed."""


def _packer(fmt):
    """Make a pack method for a struct format, raising ConversionError."""

    def pack(self, x):
        try:
            self._buffer.write(struct.pack(fmt, x))
        except struct.error as e:
            raise ConversionError(e.args[0]) from None

    return pack


class Packer:
    """Pack values into an XDR byte string."""

    def __init__(self):
        self.reset()

    def reset(self):
        self._buffer = BytesIO()

    def get_buffer(self):
        return self._buffer.getvalue()

    get_buf = get_buffer

    pack_uint = _packer(">L")
    pack_int = _packer(">l")
    pack_enum = pack_int
    pack_float = _packer(">f")
    pack_double = _packer(">d")

    def pack_bool(self, x):
        self._buffer.write(b"\0\0\0\1" if x else b"\0\0\0\0")

    def pack_uhyper(self, x):
        try:
            self.pack_uint(x >> 32 & 0xFFFFFFFF)
        except (TypeError, struct.error) as e:
            raise ConversionError(e.args[0]) from None
        try:
            self.pack_uint(x & 0xFFFFFFFF)
        except (TypeError, struct.error) as e:
            raise ConversionError(e.args[0]) from None

    pack_hyper = pack_uhyper

    def pack_fstring(self, n, s):
        if n < 0:
            raise ValueError("fstring size must be nonnegative")
        data = s[:n]
        n = ((n + 3) // 4) * 4
        data = data + (n - len(data)) * b"\0"
        self._buffer.write(data)

    pack_fopaque = pack_fstring

    def pack_string(self, s):
        n = len(s)
        self.pack_uint(n)
        self.pack_fstring(n, s)

    pack_opaque = pack_string
    pack_bytes = pack_string


class Unpacker:
    """Unpack values from an XDR byte string."""

    def __init__(self, data):
        self.reset(data)

    def reset(self, data):
        self._data = data
        self._pos = 0

    def get_position(self):
        return self._pos

    def set_position(self, position):
        self._pos = position

    def get_buffer(self):
        return self._data

    def done(self):
        if self._pos < len(self._data):
            raise Error("unextracted data remains")

    def _take(self, n):
        # Like xdrlib, the position advances even when the data runs out.
        i = self._pos
        self._pos = j = i + n
        data = self._data[i:j]
        if len(data) < n:
            raise EOFError
        return data

    def unpack_uint(self):
        return struct.unpack(">L", self._take(4))[0]

    def unpack_int(self):
        return struct.unpack(">l", self._take(4))[0]

    unpack_enum = unpack_int

    def unpack_bool(self):
        return bool(self.unpack_int())

    def unpack_uhyper(self):
        hi = self.unpack_uint()
        lo = self.unpack_uint()
        return int(hi) << 32 | lo

    def unpack_hyper(self):
        x = self.unpack_uhyper()
        if x >= 0x8000000000000000:
            x = x - 0x10000000000000000
        return x

    def unpack_float(self):
        return struct.unpack(">f", self._take(4))[0]

    def unpack_double(self):
        return struct.unpack(">d", self._take(8))[0]

    def unpack_fstring(self, n):
        if n < 0:
            raise ValueError("fstring size must be nonnegative")
        i = self._pos
        j = i + (n + 3) // 4 * 4
        if j > len(self._data):
            raise EOFError
        self._pos = j
        return self._data[i : i + n]

    unpack_fopaque = unpack_fstring

    def unpack_string(self):
        n = self.unpack_uint()
        return self.unpack_fstring(n)

    unpack_opaque = unpack_string
    unpack_bytes = unpack_string
