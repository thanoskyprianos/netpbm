# Netpbm converter

A small C program that converts images between the various
[Netpbm](https://netpbm.sourceforge.net/doc/) formats. It operates as a
Unix filter: it reads an image from standard input and writes the
converted image to standard output, so it can easily be used in pipes.

Two executables are built from the single source file `figproc.c`:

| Executable          | Purpose                                            |
| ------------------- | -------------------------------------------------- |
| `figproc_transform` | Color -> grayscale -> black & white conversions    |
| `figproc_convert`   | ASCII <-> binary conversions (e.g. for compression) |

## Supported formats

Netpbm is a family of six related image formats:

| Format | Type                | Encoding | Bits per pixel |
| ------ | ------------------- | -------- | -------------- |
| P1     | B&W                 | ASCII    | 1              |
| P2     | Grayscale           | ASCII    | 8              |
| P3     | RGB                 | ASCII    | 24             |
| P4     | B&W                 | binary   | 1              |
| P5     | Grayscale           | binary   | 8              |
| P6     | RGB                 | binary   | 24             |

The ASCII formats are easy to inspect and edit by hand but large; the
binary formats are compact but not human-readable. Values are 8-bit:
the maximum color value (`maxval`) of an image may be anywhere from 1
to 255.

## Conversions performed

`figproc_transform` reduces the amount of color information, one step at
a time:

```
P6 (RGB binary)      -> P5 (grayscale binary)   -> P4 (B&W binary)
P3 (RGB ASCII)       -> P2 (grayscale ASCII)    -> P1 (B&W ASCII)
```

`figproc_convert` changes only the encoding, not the image content:

```
P6 <-> P3    P5 <-> P2    P4 <-> P1
```

Input images that are already black & white (P1/P4) are rejected by
`figproc_transform`, since there is no color information left to remove.

## Building

Requires a C compiler (default `gcc`, overridable with `CC`). No
dependencies or libraries are needed.

```sh
make            # builds both executables
make transform  # builds figproc_transform only
make convert    # builds figproc_convert only
make clean      # removes the built executables
```

`make test` runs the test suite (see below).

## Usage

```sh
figproc_transform < image.ppm > grayscale.pgm
figproc_convert   < image.ppm > ascii.ppm
```

Because the program reads stdin and writes stdout, it composes well
with other tools:

```sh
# convert a color photo to black & white PBM, then pipe it into another tool
figproc_transform < photo.ppm | figproc_transform > photo.pbm
```

Errors are reported on stderr and the program exits with a nonzero
status; the message `Successful conversion!` on stderr confirms success.

## Behavior notes

* **Grayscale** is computed with the standard Rec. 601 luma weights
  `(299 R + 587 G + 114 B) / 1000`, rounded to the nearest integer.
* **B&W thresholding** is at 50%: values `<= (maxval + 1) / 2` become
  black (1), everything brighter becomes white (0). Black is 1, white
  is 0 in PBM.
* **Comments** (`# ...` to the end of the line) are accepted anywhere
  in the header of every format and anywhere in the raster of the ASCII
  formats, as the specification permits.
* **P4 rows** are packed MSB-first; the padding bits of the last byte
  of each row are zero.
* **Lenient parsing**: a missing delimiter before a binary raster is
  tolerated, but malformed headers, out-of-range values, and truncated
  rasters are rejected with a descriptive error message.
* Inputs are validated: magic number, width/height positivity, and
  maxval between 1 and 255; raster values may never exceed maxval, and
  P1 pixels must be 0 or 1.

## Testing

```sh
make test
```

The suite (`tests/run_tests.py`) needs only Python 3 and generates all
its fixtures itself. It covers:

* every conversion direction, including round trips
  (P6 -> P3 -> P6, P1 -> P4 -> P1, ...) that must be lossless,
* exact byte-level output for packed P4 rows (including zero padding),
* comments in headers and rasters, and comments before the binary
  raster delimiter,
* tricky raster bytes that are themselves whitespace character values,
* threshold and polarity behavior, maxval 1 and 1x1 edge cases,
* files whose final value is not followed by a newline,
* 17 error cases that must be rejected.

When the system netpbm tools are installed, the suite additionally
validates every produced file with `pamfile`.
