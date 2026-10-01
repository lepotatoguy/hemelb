# Extracted property files

HemeLB writes the results requested in the `<properties>` section of the
configuration ([XmlConfiguration.md](../../user/XmlConfiguration.md)) to
these files, one file per `<propertyoutput>`, under `results/Extracted`.
Checkpoints use the same format. The extension is `.xtr`.

To read them, use the Python tools: `hlb-dump-extracted-properties` prints
them as text, and `hlb.parsers.extraction.ExtractedProperty` loads them in
Python ([python-tools.md](../../user/python-tools.md)).

All numbers are big-endian XDR (4-byte integers and floats, 8-byte doubles
and 64-bit integers; strings are a length followed by the bytes, padded to a
multiple of 4).

## Main header
The file begins with a main header (length = 84 bytes)
* uint32 - HemeLbMagicNumber
* uint32 - ExtractionMagicNumber
* uint32 - Format version number
* double - Voxel size (metres)
* double - Time step (seconds)
* double - Mass scale (kg)
* double x 3 - Origin x,y,z components (metres)
* double - Reference pressure (Pa)
* uint64 - Total number of sites
* uint32 - Field count
* uint32 - Length of the field header that follows
  
HemeLbMagicNumber = 0x686c6221 ("hlb!"), ExtractionMagicNumber =
0x78747204 ("xtr" + 4). The version number is currently 6.

## Field header
This header has fieldCount entries and in each one:
 * XDR string - the field name
 * uint32 - number of values making up the field
 * uint32 - a type code indicating what the data type is (see below)
 * uint32 - number of offset values that follow (valid values are {0,
            1, n_values})
 * type[n_offsets] - the array of offsets, saved as the type indicated above
 * type - lattice-to-physical scale; zero indicates no scaling

## Field data type codes
The data in the main file is saved as one of the following types (see
enum in [/Code/io/formats/extraction.h](../../../Code/io/formats/extraction.h)):

 0. FLOAT
 1. DOUBLE
 2. INT32
 3. UINT32
 4. INT64
 5. UINT64

## Data section
The body of the file contains a number of entries, one per timestep recorded.
Each record consists of:
 * uint64 - timestep number
 * for each output site (as many as the total number given in the main header)
  * 3x uint32 for grid position (lattice coordinates; multiply by the
    voxel size and add the origin to get metres)
  * for each field
    * the number of values specified in the corresponding field
      header, saved as the type indicated. Decode as `(stored + offset) * scale`,
      using no scale multiplication when the stored scale is zero. Pressure
      stores a lattice pressure difference and its field offset is reference
      pressure divided by the pressure scale.

## Offset files
The offset files are a companion to this file - see
[offset.md](offset.md) for details.

## Changelog

### Version 6

Adds timestep, mass scale, reference pressure, and per-field scales. Field bodies
use lattice units. The Python reader accepts versions 4, 5, and 6; older versions
retain their physical-unit interpretation. Checkpoint loading also accepts all
three versions, restores distribution offsets, and promotes stored floats to
double precision. The geometry, lattice vector count, and offset-file layout
are checked before loading.

### Version 5

The extraction file now supports different types of data to be
serialised (to support checkpointing at full double precision) and
offsets are more configurable, although this isn't yet fully
implemented in HemeLB.

### Version 4
File stores data as 32 bit floats plus a constant offset in the
header.

Offset files were added at some point also.

### Version 3
File stores only 64 bit doubles.
