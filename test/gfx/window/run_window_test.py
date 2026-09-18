#!/usr/bin/env python3
"""Runs the gfx showcase (gfx_showcase.goose) with a real window, every way
this machine can: in-process through the compiler's TinyCC backend, and built
from the generated C with each C toolchain found, each on every GPU driver
SDL has for the platform (Direct3D 12 and Vulkan on Windows, Vulkan on Linux,
Metal on macOS). Not part of test/run_tests.py, which has to pass on machines
with no display or GPU.

Each run must exit cleanly and report all of its own checks passed (see the
showcase: what an object-ID pass and the screen read back, sparks moving,
input, a PNG written and loaded back). Then the screenshots it saved are
decoded here and checked for size and detail, and each is compared with the
same frame from the first run: the scene is deterministic, so every backend
must draw nearly the same picture.

  python test/gfx/window/run_window_test.py [--exe path/to/goose] [--quick]
      [--frames N] [--hidden] [--keep]

--quick runs only in-process on the default driver. Screenshots go to
build/gfx-window/<run>/, kept for looking at.
"""

import argparse
import os
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent.parent / "scripts"))
import toolchain as tc

SHOWCASE = HERE / "gfx_showcase.goose"


def read_png(path):
    """(width, height, rows of RGBA bytes) of an 8-bit RGB or RGBA PNG."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat = 8, b""
    width = height = channels = None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError(f"unsupported PNG: depth {depth}, color type {color}")
            channels = 4 if color == 6 else 3
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pred) & 255
        rows.append(bytes(line) if channels == 4 else
                    b"".join(bytes(line[i:i + 3]) + b"\xff" for i in range(0, stride, 3)))
        prev = line
    return width, height, rows


def describe(shot):
    """Problems with one screenshot, and its pixels for comparing."""
    try:
        w, h, rows = read_png(shot)
    except (OSError, ValueError, zlib.error) as e:
        return [f"{shot.name}: {e}"], None
    problems = []
    colors = {rows[y][x * 4:x * 4 + 3] for y in range(0, h, 3) for x in range(0, w, 3)}
    if len(colors) < 500:
        problems.append(f"{shot.name}: only {len(colors)} distinct colors")
    return problems, (w, h, rows)


def difference(a, b):
    """Mean absolute difference per channel between two images, 0 to 255."""
    (w, h, ra), (w2, h2, rb) = a, b
    if (w, h) != (w2, h2):
        return 255.0
    total, count = 0, 0
    for y in range(0, h, 2):
        la, lb = ra[y], rb[y]
        for i in range(0, w * 4, 8):
            total += abs(la[i] - lb[i]) + abs(la[i + 1] - lb[i + 1]) + abs(la[i + 2] - lb[i + 2])
            count += 3
    return total / count


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the goose compiler to use")
    ap.add_argument("--quick", action="store_true",
                    help="only in-process, on the default GPU driver")
    ap.add_argument("--frames", type=int, default=150)
    ap.add_argument("--hidden", action="store_true", help="do not show the window")
    args = ap.parse_args()
    tc.setup_console()
    exe = tc.find_goose(args.exe)
    outroot = tc.REPO_ROOT / "build" / "gfx-window"
    outroot.mkdir(parents=True, exist_ok=True)

    # How: in-process, then built by each toolchain.
    modes = []
    if tc.have_jit(exe) and tc.have_gfx(exe):
        modes.append(("jit", None))
    if not args.quick:
        ccs = list(tc.find_ccs().values())
        clang = tc.find_clang_c()
        if clang and tc.IS_WINDOWS:
            ccs.append(clang)
        for cc in ccs:
            libs = tc.gfx_link(exe, cc)
            if libs:
                modes.append((f"{cc.name}-{cc.style}", (cc, libs)))
    if not modes:
        sys.exit(f"{exe} has no gfx layer to run the showcase with")
    # Where: each GPU driver of the platform; none named is SDL's choice.
    if args.quick:
        drivers = [None]
    elif tc.IS_WINDOWS:
        drivers = ["direct3d12", "vulkan"]
    elif tc.IS_MACOS:
        drivers = ["metal"]
    else:
        drivers = ["vulkan"]

    runs, failures, reference = [], 0, {}
    gendir = outroot / "gen"
    gendir.mkdir(exist_ok=True)
    for mode, how in modes:
        argv = None
        if how is None:
            argv = [exe, "--jit", SHOWCASE]
        else:
            cc, libs = how
            cfile = gendir / f"showcase-{mode}.c"
            efile = gendir / f"showcase-{mode}{tc.EXE_SUFFIX}"
            code, out, err = tc.run_capture([exe, "-O2", "-o", cfile, SHOWCASE])
            if code != 0:
                print(out + err)
                print(f"FAIL {mode}: goose -o")
                failures += 1
                continue
            ok, log = cc.compile(cfile, efile, opt=2, libs=libs, log=gendir / f"{mode}.log")
            if not ok:
                print("\n".join(log.splitlines()[:12]))
                print(f"FAIL {mode}: {cc.desc} could not build it")
                failures += 1
                continue
            argv = [efile]
        for driver in drivers:
            label = mode + (f"/{driver}" if driver else "")
            out = outroot / label.replace("/", "-")
            out.mkdir(parents=True, exist_ok=True)
            for old in out.glob("*.png"):
                old.unlink()
            prog = ["--frames", str(args.frames), "--out", out] + (["--hidden"] if args.hidden else [])
            env = dict(os.environ)
            if driver:
                env["SDL_GPU_DRIVER"] = driver
            else:
                env.pop("SDL_GPU_DRIVER", None)
            full = argv + (["--"] if how is None else []) + prog
            import subprocess
            r = subprocess.run([str(a) for a in full], capture_output=True, env=env,
                               cwd=str(HERE))
            stdout, stderr = tc.decode(r.stdout), tc.decode(r.stderr)
            if tc.GFX_NO_DEVICE in stderr or "no GPU device" in stdout + stderr:
                print(f"skip {label}: no GPU device for this driver")
                continue
            problems = []
            if r.returncode != 0:
                problems.append(f"exit {r.returncode}")
            if "checks passed" not in stdout:
                problems += [l for l in stdout.splitlines() if "FAILED" in l] or ["no summary"]
            shots = sorted(out.glob("shot_*.png"))
            if len(shots) != 3:
                problems.append(f"{len(shots)} screenshots, not 3")
            worst = 0.0
            for shot in shots:
                bad, image = describe(shot)
                problems += bad
                if image is None:
                    continue
                if shot.name not in reference:
                    reference[shot.name] = (label, image)
                else:
                    diff = difference(reference[shot.name][1], image)
                    worst = max(worst, diff)
                    if diff > 6.0:
                        problems.append(f"{shot.name} differs from {reference[shot.name][0]}'s "
                                        f"by {diff:.1f} per channel")
            first = stdout.splitlines()[0] if stdout else ""
            if problems:
                print("\n".join(stderr.splitlines()[-10:]))
                print(f"FAIL {label}: " + "; ".join(problems))
                failures += 1
            else:
                print(f"ok   {label}: {first}; screenshots differ from the first run's by "
                      f"{worst:.2f} at most")
            runs.append(label)
    print(f"{len(runs)} run(s), {failures} failure(s); screenshots under {outroot}")
    return 1 if failures or not runs else 0


if __name__ == "__main__":
    sys.exit(main())
