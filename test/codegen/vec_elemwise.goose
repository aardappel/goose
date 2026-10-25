// Elementwise arithmetic (spec §6.1) on the shapes the C backend computes
// as one vector where the C compiler has vector extensions (GS_VEC,
// runtime.h): structs and fixed arrays of 2 to 16 floats of one width in 16,
// 32 or 64 bytes with no padding. Lane by lane that must give exactly what
// the member-by-member code gives, so every build prints the same: clang and
// gcc with and without GS_VEC, MSVC and the JIT, which never vectorize. The
// shapes next to them that don't qualify (three doubles, mixed widths,
// integers) run member by member everywhere. Simd functions do the same
// arithmetic in their AVX2 and AVX-512 versions. The runner's clang_vec also
// checks which shapes the generated C vectorizes and builds it with GS_VEC
// on and off; the simd functions are what failed to compile on x86-64 Linux
// and macOS when vectors crossed a call.

import std;

struct D2 { x: f64, y: f64 }
struct D4 { a: f64, b: f64, c: f64, d: f64 }
struct C4 { re: D4, im: D4 }                 // two D4s: eight doubles
struct F4 { a: f32, b: f32, c: f32, d: f32 }
struct F8 { lo: F4, hi: F4 }
struct D3 { x: f64, y: f64, z: f64 }         // 24 bytes: not a vector
struct Mixed { a: f64, b: f32, c: f32 }      // two widths: not a vector
struct I4 { a: i32, b: i32, c: i32, d: i32 } // integers: not a vector
struct Body { pos: D4, vel: D4, mass: f64 }

fn d4(a: f64, b: f64, c: f64, d: f64) -> D4 { D4 { a, b, c, d } }

// The same arithmetic inside simd functions, whose AVX2 and AVX-512 versions
// are built for wider vectors than the code around them.
simd fn add(a: D4, b: D4) -> D4 { a + b }

simd fn mix(xs: D4[:], ys: D4[:], out: D4[:]) {
    assert(xs.len == ys.len && out.len == xs.len);
    for x, i in xs { out[i] = x * ys[i] + x / 2.0 - -ys[i]; }
}

simd fn mix_wide(c: C4, h: F8) -> C4 {
    let g = h * h - h;
    let k = g.lo.a * 1.0;
    return c * c + c / (k + 1.0);
}

fn main() {
    let u = d4(1.5, -2.0, 0.1, 1.0e300);
    let v = d4(0.25, 3.0, 0.2, 1.0e10);
    print(u + v, " ", u - v, " ", u * v, " ", u / v, " ", -u);
    // A scalar on either side (* and / broadcast one), where the order matters.
    print(u * 2.0, " ", 2.0 * u, " ", u / 4.0, " ", 4.0 / u);
    // IEEE: signed zero, infinities and NaN, lane by lane.
    let z = d4(0.0, -0.0, 1.0, -1.0);
    print(-z, " ", z / 0.0, " ", z * 0.0, " ", (z / 0.0) - (z / 0.0));

    // Aliasing: the destination is an operand, or a scalar reads one of the
    // destination's members (v = v * v.a must use the old a everywhere).
    var w = d4(2.0, 3.0, 5.0, 7.0);
    w = w + w;
    print(w);
    w = w * w.a;
    print(w);
    w = w.d / w;
    print(w);
    w += d4(1.0, 1.0, 1.0, 1.0);
    w -= u;
    w *= 0.5;
    w /= v;
    print(w);

    // Inside other values: a field, an array element.
    var b = Body { pos: d4(0.0, 0.0, 0.0, 0.0), vel: d4(1.0, 2.0, 3.0, 4.0), mass: 2.0 };
    for i in 3 { b.pos = b.pos + b.vel * 0.5; b.vel = b.vel - b.pos / b.mass; }
    print(b.pos, " ", b.vel);
    var arr: D4[>..] = [];
    for i in 3 { arr.push(d4(i * 1.0, i + 0.5, -i * 1.0, i * i * 1.0)); }
    for &e in arr { e = e * e + e; }
    arr[1] = arr[0] - arr[2];
    print(arr);

    // Two, eight and the f32 widths.
    let p = D2 { 1.0, -3.5 };
    let q = D2 { 0.5, 2.0 };
    print(p * q - p, " ", -(p / q));
    let c = C4 { re: u, im: v };
    let c2 = c * c + c;
    print(c2.re, " ", c2.im, " ", -c);
    let f = F4 { 1.5, -0.25, 3.0, 1.0e30 };
    let g = F4 { 0.5, 4.0, -2.0, 1.0e10 };
    print(f + g, " ", f * g, " ", f / g, " ", 3.0 / f);
    let h = F8 { lo: f, hi: g };
    print(h * h - h);

    // Fixed arrays of floats.
    let a4: f64[4] = [1.0, 2.0, 3.0, 4.0];
    let b4: f64[4] = [0.5, -1.0, 8.0, 0.0];
    print(a4 * b4 + a4, " ", -(a4 / b4));
    let a16: f32[16] = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0];
    print(a16 * a16 - a16 / 2.0);

    // Inside simd functions.
    print(add(u, v), " ", add(w, w));
    var ys: D4[>..] = [];
    var outs: D4[>..] = [];
    for e in arr { ys.push(e / 3.0); outs.push(d4(0.0, 0.0, 0.0, 0.0)); }
    mix(arr, ys, outs);
    print(outs);
    let mw = mix_wide(c, h);
    print(mw.re, " ", mw.im);

    // Shapes that stay member by member.
    let d3 = D3 { 1.0, 2.0, 3.0 };
    let m = Mixed { 1.5, 2.5, -1.0 };
    let i4 = I4 { 1, -2, 3, 4 };
    print(d3 * d3 + d3, " ", m * m - m, " ", i4 * i4 + i4, " ", -i4);
}
