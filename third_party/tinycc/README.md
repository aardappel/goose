# TinyCC

This directory holds the sources of the Tiny C Compiler that
`cmake/libtcc.cmake` builds into the compiler as its in-process C backend
(`docs/design/jit_backend.md`), copied from
[TinyCC/tinycc](https://github.com/TinyCC/tinycc) at commit
`0fb54300b56512754221d80adda85ddb9815bceb` (2026-09-04, `release_0_9_26-2610`).
It contains the `.c`, `.h` and `.def` files of the repository root,
`include/`, `lib/`, `win32/include/` and `win32/lib/`, together with
`VERSION`, `COPYING`, `RELICENSING` and upstream's `README`. Its tests,
examples, documentation and build scripts are left out; the CMake file
replaces its configure and makefile.

The backend is not optional: every Goose compiler is built with it.

License: LGPL 2.1 (`COPYING`), with the relicensing notes in `RELICENSING`.

Local changes:

- `libtcc.c`: `tcc_set_output_type` honors `nostdlib_paths` for Windows
  system directories and macOS SDK discovery, as it does for configured
  library paths. The embedded JIT supplies its runtime symbols and support
  archive itself.
- `arm64-gen.c`: use Apple's natural stack size and alignment for fixed
  floating-point arguments, including homogeneous float aggregates. This
  fixes a trailing float being passed at offset 16 instead of 12 after a
  spilled three-float struct. Variadic arguments retain eight-byte slots.
  `test/packages/pkg_float_abi.goose` checks calls into native C through the
  in-process backend as well as native builds.
- `lib/libtcc1.c`, `tccgen.c`: round an unsigned 64-bit integer of 2^63 or
  more to float or double once, at run time (`__floatundisf`,
  `__floatundidf`), and a 64-bit integer constant to float once when folding
  it. Both went through `long double`, which is no wider than `double` on
  Windows (and, for the folding, on Apple ARM64), so they rounded twice and
  could be an ulp off.
  `test/runtime/float_conversions_unchecked.goose` checks values past 2^63.

To update, copy the same files from a newer TinyCC commit and record that
commit here. Preserve the local changes until they are included upstream.
