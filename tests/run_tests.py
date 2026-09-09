#!/usr/bin/env python3
"""Test suite for figproc_transform and figproc_convert.

Run with:  make test   (or  python3 tests/run_tests.py)
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRANSFORM = os.path.join(ROOT, "figproc_transform")
CONVERT = os.path.join(ROOT, "figproc_convert")

PASS = 0
FAIL = 0


def ok(name):
    global PASS
    PASS += 1
    print("PASS: " + name)


def bad(name, msg):
    global FAIL
    FAIL += 1
    print("FAIL: %s: %s" % (name, msg))


def run(binary, data):
    return subprocess.run([binary], input=data, capture_output=True)


def run_ok(name, binary, data):
    p = run(binary, data)
    if p.returncode != 0:
        bad(name, "expected success, got rc=%d: %s"
            % (p.returncode, p.stderr.decode(errors="replace").strip()))
        return None
    return p.stdout


def run_fail(name, binary, data):
    p = run(binary, data)
    if p.returncode == 0:
        bad(name, "expected failure but conversion succeeded")
        return None
    return p.stdout


def luminosity(r, g, b):
    return (299 * r + 587 * g + 114 * b + 500) // 1000


def threshold(maxval):
    return (maxval + 1) // 2


def make_pnm(typ, width, height, maxval, pixels):
    """Serializes an image. pixels: list of ints (P1/P2/P5) or (r,g,b)
       tuples (P3/P6). Returns bytes."""
    out = b"P%d\n%d %d" % (typ, width, height)
    if typ not in (1, 4):
        out += b" %d" % maxval
    out += b"\n"

    if typ == 1:
        out += b" ".join(str(v).encode() for v in pixels) + b"\n"
    elif typ == 2:
        for r in range(height):
            row = pixels[r * width:(r + 1) * width]
            out += b" ".join(str(v).encode() for v in row) + b"\n"
    elif typ == 3:
        for r in range(height):
            vals = []
            for j in range(width):
                vals += list(pixels[r * width + j])
            out += b" ".join(str(v).encode() for v in vals) + b"\n"
    elif typ == 4:
        rowbytes = (width + 7) // 8
        for r in range(height):
            row = bytearray(rowbytes)
            for j in range(width):
                row[j // 8] |= pixels[r * width + j] << (7 - (j % 8))
            out += bytes(row)
    elif typ == 5:
        out += bytes(pixels)
    elif typ == 6:
        out += bytes(v for pix in pixels for v in pix)
    return out


def parse_pnm(data):
    """Parses a PNM image. Returns (typ, width, height, maxval, pixels)."""
    pos = 0
    n = len(data)

    def next_token():
        nonlocal pos
        while True:
            if pos < n and data[pos:pos + 1] == b"#":
                nl = data.find(b"\n", pos)
                pos = nl + 1 if nl != -1 else n
                continue
            if pos < n and data[pos:pos + 1].isspace():
                pos += 1
                continue
            break
        start = pos
        while pos < n and not data[pos:pos + 1].isspace():
            pos += 1
        return data[start:pos]

    magic = next_token()
    if not magic or magic[0:1] != b"P":
        raise ValueError("missing magic number")
    typ = int(magic[1:])
    width = int(next_token())
    height = int(next_token())
    maxval = 1 if typ in (1, 4) else int(next_token())

    if typ in (4, 5, 6):
        if pos < n and data[pos:pos + 1].isspace():
            pos += 1
        rest = data[pos:]
        if typ == 4:
            rowbytes = (width + 7) // 8
            pixels = []
            for r in range(height):
                row = rest[r * rowbytes:(r + 1) * rowbytes]
                if len(row) < rowbytes:
                    raise ValueError("truncated P4 raster")
                for j in range(width):
                    pixels.append((row[j // 8] >> (7 - (j % 8))) & 1)
        else:
            count = width * height * (3 if typ == 6 else 1)
            vals = list(rest[:count])
            if len(vals) < count:
                raise ValueError("truncated raster")
            if typ == 6:
                pixels = [(vals[i], vals[i + 1], vals[i + 2])
                          for i in range(0, count, 3)]
            else:
                pixels = vals
    else:
        toks = []
        while True:
            t = next_token()
            if not t:
                break
            toks.append(int(t))
        count = width * height * (3 if typ == 3 else 1)
        vals = toks[:count]
        if len(vals) < count:
            raise ValueError("truncated raster")
        if typ == 3:
            pixels = [(vals[i], vals[i + 1], vals[i + 2])
                      for i in range(0, count, 3)]
        else:
            pixels = vals

    return typ, width, height, maxval, pixels


def raster_body(data):
    """Returns the raw raster section of a PNM file (everything after the
       single whitespace that follows the last header token)."""
    pos = 0
    n = len(data)

    def skip_ws_comments():
        nonlocal pos
        while True:
            if pos < n and data[pos:pos + 1] == b"#":
                nl = data.find(b"\n", pos)
                pos = nl + 1 if nl != -1 else n
                continue
            if pos < n and data[pos:pos + 1].isspace():
                pos += 1
                continue
            break

    start = pos
    while pos < n and not data[pos:pos + 1].isspace():
        pos += 1
    magic = data[start:pos]
    skip_ws_comments()
    tokens = 2 if magic in (b"P1", b"P4") else 3
    for _ in range(tokens):
        while pos < n and not data[pos:pos + 1].isspace():
            pos += 1
        skip_ws_comments()
    if pos < n and data[pos:pos + 1].isspace():
        pos += 1
    return data[pos:]


def pamfile_check(name, data, want_header):
    """Validates the output with the system netpbm tool pamfile."""
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(data)
        path = f.name
    try:
        p = subprocess.run(["pamfile", path], capture_output=True)
        if p.returncode != 0:
            bad(name, "pamfile rejected output: %s"
                % p.stderr.decode(errors="replace").strip())
            return
        line = p.stdout.decode(errors="replace").split("\t")[-1].strip()
        if want_header not in line:
            bad(name, "pamfile header mismatch: %r not in %r" % (want_header, line))
    finally:
        os.unlink(path)


# ---------------------------------------------------------------------------
# test cases
# ---------------------------------------------------------------------------

def test_transform_p6_p5_tricky_first_bytes():
    name = "transform P6->P5 (first raster byte is whitespace value)"
    w, h, mv = 7, 2, 255
    pix = [(10, 0, 0), (0, 200, 0), (0, 0, 255), (255, 255, 255),
           (0, 0, 0), (9, 13, 32), (128, 128, 128),
           (32, 10, 9), (255, 0, 255), (1, 2, 3), (100, 150, 200),
           (250, 200, 150), (0, 10, 0), (13, 9, 32)]
    src = make_pnm(6, w, h, mv, pix)
    out = run_ok(name, TRANSFORM, src)
    if out is None:
        return
    typ, ow, oh, omv, gray = parse_pnm(out)
    want = [luminosity(*p) for p in pix]
    if typ != 5 or (ow, oh, omv) != (w, h, mv):
        bad(name, "wrong header: P%d %d %d %d" % (typ, ow, oh, omv))
        return
    if gray != want:
        bad(name, "wrong gray values: got %s want %s" % (gray, want))
        return
    ok(name)


def test_transform_p5_p4():
    name = "transform P5->P4 (padding bits, polarity, whitespace bytes)"
    w, h, mv = 9, 2, 255
    rows = [[32, 127, 128, 129, 255, 10, 9, 1, 64],
            [200, 201, 202, 100, 0, 129, 128, 5, 77]]
    src = make_pnm(5, w, h, mv, rows[0] + rows[1])
    out = run_ok(name, TRANSFORM, src)
    if out is None:
        return
    typ, ow, oh, omv, bits = parse_pnm(out)
    th = threshold(mv)
    want = [1 if v <= th else 0 for v in rows[0] + rows[1]]
    if typ != 4 or (ow, oh) != (w, h):
        bad(name, "wrong header: P%d %d %d" % (typ, ow, oh))
        return
    if bits != want:
        bad(name, "wrong bit pattern: got %s want %s" % (bits, want))
        return
    # check padding bits are zero and the bytes are exactly as expected
    body = raster_body(out)
    if body != b"\xe7\x80\x1b\x80":
        bad(name, "wrong raw bytes: %r" % body)
        return
    ok(name)


def test_convert_p4_p1():
    name = "convert P4->P1"
    w, h = 9, 2
    bits = [1, 1, 1, 0, 0, 1, 1, 1, 1,
            0, 0, 0, 1, 1, 0, 1, 1, 1]
    src = make_pnm(4, w, h, 1, bits)
    out = run_ok(name, CONVERT, src)
    if out is None:
        return
    body = raster_body(out)
    if body != b"1 1 1 0 0 1 1 1 1\n0 0 0 1 1 0 1 1 1\n":
        bad(name, "wrong text output: %r" % body)
        return
    ok(name)


def test_convert_p1_p4_roundtrip():
    name = "convert P1<->P4 round trip (zero padding)"
    w, h = 5, 2
    bits = [1, 0, 1, 1, 0, 0, 0, 1, 0, 1]
    src = make_pnm(1, w, h, 1, bits)
    p4 = run_ok(name, CONVERT, src)
    if p4 is None:
        return
    body = p4[p4.index(b"\n", p4.index(b"\n") + 1) + 1:]
    if body != b"\xb0\x28":
        bad(name, "wrong P4 bytes (padding must be zero): %r" % body)
        return
    p1 = run_ok(name, CONVERT, p4)
    if p1 is None:
        return
    try:
        typ, ow, oh, _, got = parse_pnm(p1)
    except ValueError as e:
        bad(name, "cannot parse P1 output: %s" % e)
        return
    if (typ, ow, oh) != (1, w, h) or got != bits:
        bad(name, "round trip mismatch: %s" % got)
        return
    ok(name)


def test_convert_p6_p3_roundtrip():
    name = "convert P6<->P3 round trip"
    w, h, mv = 4, 3, 200
    pix = []
    for i in range(w * h):
        pix.append(((i * 37) % (mv + 1), (i * 13 + 32) % (mv + 1),
                    (i * 7 + 10) % (mv + 1)))
    src = make_pnm(6, w, h, mv, pix)
    p3 = run_ok(name, CONVERT, src)
    if p3 is None:
        return
    try:
        typ, ow, oh, omv, got = parse_pnm(p3)
    except ValueError as e:
        bad(name, "cannot parse P3 output: %s" % e)
        return
    if (typ, ow, oh, omv) != (3, w, h, mv) or got != pix:
        bad(name, "P3 values mismatch")
        return
    p6 = run_ok(name, CONVERT, p3)
    if p6 is None:
        return
    body = raster_body(p6)
    want_body = raster_body(make_pnm(6, w, h, mv, pix))
    if body != want_body:
        bad(name, "P6 raster bytes differ after round trip")
        return
    ok(name)


def test_convert_p2_p5_roundtrip():
    name = "convert P2<->P5 round trip (maxval 17)"
    w, h, mv = 6, 2, 17
    vals = [0, 1, 8, 9, 16, 17, 3, 5, 7, 11, 13, 15]
    src = make_pnm(2, w, h, mv, vals)
    p5 = run_ok(name, CONVERT, src)
    if p5 is None:
        return
    body = p5[p5.index(b"\n", p5.index(b"\n", p5.index(b"\n") + 1) + 1) + 1:]
    if body != bytes(vals):
        bad(name, "P5 raster mismatch: %r" % body)
        return
    p2 = run_ok(name, CONVERT, p5)
    if p2 is None:
        return
    try:
        _, _, _, _, got = parse_pnm(p2)
    except ValueError as e:
        bad(name, "cannot parse P2 output: %s" % e)
        return
    if got != vals:
        bad(name, "P2 values mismatch")
        return
    ok(name)


def test_transform_p3_p2():
    name = "transform P3->P2"
    w, h, mv = 3, 2, 255
    pix = [(100, 0, 0), (0, 100, 0), (0, 0, 100),
           (255, 255, 255), (0, 0, 0), (50, 50, 50)]
    src = make_pnm(3, w, h, mv, pix)
    out = run_ok(name, TRANSFORM, src)
    if out is None:
        return
    typ, ow, oh, omv, gray = parse_pnm(out)
    want = [luminosity(*p) for p in pix]
    if (typ, ow, oh, omv) != (2, w, h, mv) or gray != want:
        bad(name, "wrong result: %s" % gray)
        return
    ok(name)


def test_transform_p2_p1_threshold():
    name = "transform P2->P1 (threshold and polarity)"
    w, h, mv = 4, 1, 255
    vals = [0, 128, 129, 255]
    src = make_pnm(2, w, h, mv, vals)
    out = run_ok(name, TRANSFORM, src)
    if out is None:
        return
    typ, ow, oh, _, bits = parse_pnm(out)
    th = threshold(mv)
    want = [1 if v <= th else 0 for v in vals]
    if (typ, ow, oh) != (1, w, h) or bits != want:
        bad(name, "wrong result: got %s want %s" % (bits, want))
        return
    ok(name)


def test_comments_everywhere():
    name = "comments in header and raster"
    w, h, mv = 3, 2, 255
    vals = [10, 20, 30, 40, 50, 60]
    src = (b"P2\n# after magic\n3 2\n# between width and height\n"
           b"255 # after maxval\n"
           b"10 20 # inline comment\n30\n# full line in raster\n40 50 60\n")
    out = run_ok(name, CONVERT, src)
    if out is None:
        return
    try:
        _, _, _, _, got = parse_pnm(out)
    except ValueError as e:
        bad(name, "cannot parse output: %s" % e)
        return
    if got != vals:
        bad(name, "wrong values: %s" % got)
        return
    ok(name)


def test_comment_before_binary_delimiter():
    name = "P5 comment between maxval and raster delimiter"
    w, h, mv = 2, 1, 255
    vals = [0x20, 0x7f]  # first raster byte is a whitespace character value
    src = b"P5\n2 1 255 # comment\n" + bytes(vals)
    out = run_ok(name, CONVERT, src)
    if out is None:
        return
    try:
        _, _, _, _, got = parse_pnm(out)
    except ValueError as e:
        bad(name, "cannot parse output: %s" % e)
        return
    if got != vals:
        bad(name, "wrong values: got %s want %s" % (got, vals))
        return
    ok(name)


def test_comment_attached_binary_delimiter():
    name = "P5 comment attached to maxval, ws-valued first raster byte"
    w, h, mv = 2, 1, 255
    vals = [0x20, 0x7f]
    src = b"P5\n2 1 255# comment\n" + bytes(vals)
    out = run_ok(name, CONVERT, src)
    if out is None:
        return
    try:
        _, _, _, _, got = parse_pnm(out)
    except ValueError as e:
        bad(name, "cannot parse output: %s" % e)
        return
    if got != vals:
        bad(name, "wrong values: got %s want %s" % (got, vals))
        return
    ok(name)


def test_maxval_one():
    name = "maxval 1 images"
    w, h = 3, 2
    vals = [0, 1, 0, 1, 1, 0]
    src = make_pnm(2, w, h, 1, vals)
    out = run_ok(name, CONVERT, src)  # P2 -> P5
    if out is None:
        return
    body = raster_body(out)
    if body != bytes(vals):
        bad(name, "P5 raster mismatch: %r" % body)
        return
    p1 = run_ok(name, TRANSFORM, src)  # P2 -> P1: 0 stays black, 1 is white
    if p1 is None:
        return
    try:
        _, _, _, _, got = parse_pnm(p1)
    except ValueError as e:
        bad(name, "cannot parse P1 output: %s" % e)
        return
    if got != [1, 1, 1, 1, 1, 1]:  # threshold 1: both 0 and 1 are "dark"
        bad(name, "P1 threshold wrong: %s" % got)
        return
    ok(name)


def test_1x1():
    name = "1x1 images"
    src = make_pnm(6, 1, 1, 255, [(12, 34, 56)])
    out = run_ok(name, TRANSFORM, src)
    if out is None:
        return
    _, _, _, _, got = parse_pnm(out)
    if got != [luminosity(12, 34, 56)]:
        bad(name, "wrong value: %s" % got)
        return
    src1 = make_pnm(1, 1, 1, 1, [1])
    out1 = run_ok(name, CONVERT, src1)
    if out1 is None:
        return
    body = out1[out1.index(b"\n", out1.index(b"\n") + 1) + 1:]
    if body != b"\x80":
        bad(name, "wrong P4 byte: %r" % body)
        return
    ok(name)


def test_no_trailing_newline():
    name = "P2 whose last value ends at EOF (no trailing newline)"
    w, h, mv = 2, 1, 255
    src = b"P2\n2 1\n255\n100 200"  # no newline at end
    out = run_ok(name, CONVERT, src)
    if out is None:
        return
    body = raster_body(out)
    if body != b"\x64\xc8":
        bad(name, "wrong raster: %r" % body)
        return
    ok(name)


def test_pamfile_crosscheck():
    name = "system pamfile accepts all outputs"
    pix6 = [(1, 2, 3), (4, 5, 6), (7, 8, 9), (10, 11, 12)]
    outputs = [
        (run_ok(name + " (P6->P5)", TRANSFORM, make_pnm(6, 2, 2, 255, pix6)),
         "PGM raw, 2 by 2  maxval 255"),
        (run_ok(name + " (P6->P3)", CONVERT, make_pnm(6, 2, 2, 255, pix6)),
         "PPM plain, 2 by 2  maxval 255"),
    ]
    for data, want in outputs:
        if data is not None:
            pamfile_check(name, data, want)
    # P2 -> P1 and P1 -> P4
    o = run_ok(name + " (P2->P1)", TRANSFORM, make_pnm(2, 3, 2, 255,
                [0, 128, 129, 255, 1, 254]))
    if o is not None:
        pamfile_check(name, o, "PBM plain, 3 by 2")
    o = run_ok(name + " (P1->P4)", CONVERT, make_pnm(1, 3, 2, 1,
                [1, 0, 1, 0, 1, 0]))
    if o is not None:
        pamfile_check(name, o, "PBM raw, 3 by 2")
    ok(name)


# --------------------------- error handling tests -------------------------

def test_errors():
    cases = [
        ("bad magic number", b"Q6\n1 1 255\n\x00\x00\x00", TRANSFORM),
        ("unsupported type P7", b"P7\n1 1 255\n\x00\x00\x00", TRANSFORM),
        ("maxval 0", b"P5\n1 1 0\n\x00", TRANSFORM),
        ("maxval 300", b"P5\n1 1 300\n\x00", TRANSFORM),
        ("width 0", b"P5\n0 1 255\n\x00", TRANSFORM),
        ("height 0", b"P5\n1 0 255\n\x00", TRANSFORM),
        ("P1 value 2 in CONVERT", b"P1\n1 1\n2\n", CONVERT),
        ("P2 value over maxval in CONVERT", b"P2\n1 1 100\n101\n", CONVERT),
        ("P5 byte over maxval in TRANSFORM", b"P5\n1 1 100\n\xc8", TRANSFORM),
        ("P6 byte over maxval in TRANSFORM", b"P6\n1 1 100\n\xc8\x00\x00",
         TRANSFORM),
        ("truncated P5 raster", b"P5\n2 2 255\n\x01\x02\x03", CONVERT),
        ("truncated P6 raster", b"P6\n1 1 255\n\x01\x02", CONVERT),
        ("TRANSFORM on P1", b"P1\n1 1\n0\n", TRANSFORM),
        ("TRANSFORM on P4", b"P4\n1 1\n\x80", TRANSFORM),
        ("empty input", b"", CONVERT),
        ("junk after magic", b"P6x\n1 1 255\n\x00\x00\x00", CONVERT),
        ("garbage in ASCII raster", b"P2\n2 1 255\n10 abc\n", CONVERT),
    ]
    for label, data, binary in cases:
        name = "error: " + label
        p = run(binary, data)
        if p.returncode == 0:
            bad(name, "expected failure but conversion succeeded")
        else:
            ok(name)


def main():
    test_transform_p6_p5_tricky_first_bytes()
    test_transform_p5_p4()
    test_convert_p4_p1()
    test_convert_p1_p4_roundtrip()
    test_convert_p6_p3_roundtrip()
    test_convert_p2_p5_roundtrip()
    test_transform_p3_p2()
    test_transform_p2_p1_threshold()
    test_comments_everywhere()
    test_comment_before_binary_delimiter()
    test_comment_attached_binary_delimiter()
    test_maxval_one()
    test_1x1()
    test_no_trailing_newline()
    if shutil.which("pamfile"):
        test_pamfile_crosscheck()
    test_errors()

    print()
    print("%d passed, %d failed" % (PASS, FAIL))
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
