# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.
import math
import random
import warnings

import pytest

from hlb.utils import pyxdr

with warnings.catch_warnings():
    warnings.simplefilter("ignore", DeprecationWarning)
    try:
        import xdrlib
    except ImportError:  # removed in Python 3.13
        xdrlib = None


def test_known_encodings():
    p = pyxdr.Packer()
    p.pack_uint(1)
    p.pack_int(-2)
    p.pack_uhyper(2**40 + 3)
    p.pack_hyper(-1)
    p.pack_float(1.5)
    p.pack_double(-0.25)
    p.pack_string(b"abcde")
    p.pack_bool(True)
    expected = bytes.fromhex(
        "00000001"
        "fffffffe"
        "0000010000000003"
        "ffffffffffffffff"
        "3fc00000"
        "bfd0000000000000"
        "00000005"
        "6162636465000000"
        "00000001"
    )
    assert p.get_buffer() == expected

    u = pyxdr.Unpacker(expected)
    assert u.unpack_uint() == 1
    assert u.unpack_int() == -2
    assert u.unpack_uhyper() == 2**40 + 3
    assert u.unpack_hyper() == -1
    assert u.unpack_float() == 1.5
    assert u.unpack_double() == -0.25
    assert u.unpack_string() == b"abcde"
    assert u.unpack_bool() is True
    u.done()


def test_errors():
    with pytest.raises(pyxdr.ConversionError):
        pyxdr.Packer().pack_uint(-1)
    with pytest.raises(pyxdr.ConversionError):
        pyxdr.Packer().pack_uhyper(1.5)
    with pytest.raises(EOFError):
        pyxdr.Unpacker(b"\0\0\0").unpack_uint()
    with pytest.raises(EOFError):
        pyxdr.Unpacker(b"\0\0\0\5ab").unpack_string()
    with pytest.raises(pyxdr.Error):
        pyxdr.Unpacker(b"\0\0\0\0\0").done()


PACK_VALUES = {
    "pack_uint": [0, 1, 2**32 - 1, 2**32, -1, 1.0, "x"],
    "pack_int": [0, -(2**31), 2**31 - 1, 2**31, 7.0],
    "pack_uhyper": [0, 2**64 - 1, 2**64 + 5, -1, 1.5],
    "pack_hyper": [-(2**63), 2**63 - 1, -5],
    "pack_float": [0.0, -1.25, 3.4e38, 1e39, math.inf, math.nan, 2, "x"],
    "pack_double": [0.0, 1e308, -math.inf, math.nan, 3, "x"],
    "pack_bool": [True, False, 0, 5],
    "pack_string": [b"", b"a", b"abcd", b"abcde", bytes(range(9))],
}


def outcome(call):
    try:
        result = call()
    except Exception as e:  # compare exception type and message
        return ("raised", type(e).__name__, str(e))
    if isinstance(result, float) and math.isnan(result):
        return ("nan",)
    return ("ok", result)


@pytest.mark.skipif(xdrlib is None, reason="xdrlib not available")
@pytest.mark.parametrize("method", sorted(PACK_VALUES))
def test_packing_matches_xdrlib(method):
    for value in PACK_VALUES[method]:
        ours, theirs = pyxdr.Packer(), xdrlib.Packer()
        a = outcome(lambda: getattr(ours, method)(value))
        b = outcome(lambda: getattr(theirs, method)(value))
        assert a == b, (method, value)
        assert ours.get_buffer() == theirs.get_buffer(), (method, value)


UNPACK_METHODS = [
    "unpack_uint",
    "unpack_int",
    "unpack_uhyper",
    "unpack_hyper",
    "unpack_float",
    "unpack_double",
    "unpack_bool",
    "unpack_string",
]


@pytest.mark.skipif(xdrlib is None, reason="xdrlib not available")
def test_unpacking_matches_xdrlib_on_random_data():
    rng = random.Random(1234)
    for _ in range(3000):
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 40)))
        if rng.random() < 0.3 and len(data) >= 4:
            # A short string length so unpack_string often succeeds
            data = bytes([0, 0, 0, rng.randrange(8)]) + data[4:]
        ours, theirs = pyxdr.Unpacker(data), xdrlib.Unpacker(data)
        for _ in range(rng.randrange(1, 6)):
            method = rng.choice(UNPACK_METHODS)
            assert outcome(getattr(ours, method)) == outcome(getattr(theirs, method))
            assert ours.get_position() == theirs.get_position()
        assert outcome(ours.done) == outcome(theirs.done)
