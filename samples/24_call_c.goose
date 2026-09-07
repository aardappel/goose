// Calling C. An `extern fn` binds a Goose signature to a C symbol: libm's
// functions arrive this way (the `math` module is nothing but such
// declarations), and so does any C the program brings along. What crosses
// the boundary is exactly what has a plain C shape: scalars, flat
// fixed-size structs (by value, or through a reference as a pointer),
// slices of those (as a data-pointer-and-length struct), and a `u8[>..]&`
// string builder, which C appends to. The C for this sample is call_c.h.
//
// Build and run (from this directory):
//   goose --include call_c.h -o call_c.c 24_call_c.goose
//   cc call_c.c -o call_c -lm -pthread && ./call_c   (or: cl call_c.c && call_c)

import std;

struct Stats { lo: i32, hi: i32, mean: f64 }

// libm directly, without the math module: the string is the C symbol.
extern "cbrt" fn cube_root(x: f64) -> f64;
extern fn hypot(x: f64, y: f64) -> f64;
// `abs` rather than `labs`: C's `long` is 32 bits on Windows and 64 on Linux
// and macOS, so it has no one Goose type, where `int` is i32 everywhere.
extern "abs" fn c_abs(x: i32) -> i32;

// From call_c.h: a slice in, a scalar out.
extern fn crc32_bytes(s: u8[:]) -> u32;
// A slice in, a struct filled through a reference.
extern fn stats_of(xs: i32[:], out: Stats&);
// A builder the C side appends to.
extern fn c_version(out: u8[>..]&);

fn main() {
    // libm may return a neighboring representable value for cbrt(27), so
    // give the display a precision instead of depending on its last bit.
    var cube: u8[>..] = [];
    format_flt(cube, cube_root(27.0), 3);
    print(cube, " ", hypot(3.0, 4.0), " ", c_abs(-42));

    // Any u8 array or slice is a u8[:] at the call: a literal, a builder,
    // a sub-range.
    print(crc32_bytes("The quick brown fox jumps over the lazy dog"));
    var text: u8[>..] = [];
    for i in 1000 { format(text, i, ","); }
    print(text.len, " bytes, crc ", crc32_bytes(text), " / ", crc32_bytes(text[..100]));

    var xs: i32[>..] = [];
    var r = rng { 5 };
    for i in 100 { xs.push(r.rand_int(1000) as i32 - 500); }
    var st = Stats { lo: 0, hi: 0, mean: 0.0 };
    stats_of(xs, st);
    print(st);
    stats_of(xs[..10], st);
    print(st.lo, " ", st.hi, " ", st.mean);

    var v: u8[>..] = [];
    v.append("built against ");
    c_version(v);
    print(v);
}
