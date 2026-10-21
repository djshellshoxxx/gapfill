#!/usr/bin/env python3
"""Compare two directories of planar float32 golden renders (A.f32 .. F.f32).

  compare_golden_dirs.py <dirA> <dirB> [--max-abs 1e-6]

Prints max abs / RMS difference per scenario; exit 1 if any max abs exceeds the limit.
"""
import array, math, pathlib, sys


def load(p):
    a = array.array("f")
    a.frombytes(pathlib.Path(p).read_bytes())
    return a


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    limit = 1e-6
    if "--max-abs" in sys.argv:
        limit = float(sys.argv[sys.argv.index("--max-abs") + 1])
        args = [a for a in args if a != sys.argv[sys.argv.index("--max-abs") + 1]]
    da, db = pathlib.Path(args[0]), pathlib.Path(args[1])
    bad = False
    for sid in "ABCDEF":
        fa, fb = da / f"{sid}.f32", db / f"{sid}.f32"
        if not fa.exists() or not fb.exists():
            print(sid, "missing"); bad = True; continue
        a, b = load(fa), load(fb)
        if len(a) != len(b):
            print(sid, "length mismatch", len(a), len(b)); bad = True; continue
        mx = ss = 0.0
        for x, y in zip(a, b):
            d = abs(x - y)
            mx = max(mx, d); ss += d * d
        rms = math.sqrt(ss / max(1, len(a)))
        print(f"{sid} max_abs={mx:.3e} rms={rms:.3e} {'OK' if mx <= limit else 'DIFF'}")
        bad |= mx > limit
    sys.exit(1 if bad else 0)


main()
