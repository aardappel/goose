// std's scalar helpers and math's, vec's, mat's and quat's functions keep no bounds
// check, so a checkfree function can call them (spec §7.14) at every type they take.
// stdlib_math, stdlib_vec, stdlib_mat and stdlib_quat check theirs from a checkfree
// main; this adds std's and the overloads those do not reach.
import std;
import math;
import vec;
import mat;
import quat;

struct P { x: i64 }
var nodes: P[>..] = [];
type NodeId = u32 in nodes;

checkfree fn floats(x: f64, y: f32) {
    let half: f32 = 0.5;
    print(abs(x), " ", sign(x), " ", min(x, 1.0), " ", max(x, -1.0), " ", clamp(x, -0.5, 0.5), " ",
          lerp(0.0, x, 0.5));                                          // 2.0 -1.0 -2.0 -1.0 -0.5 -1.0
    print(abs(y), " ", sign(y), " ", min(y, 1.0), " ", max(y, -1.0), " ", clamp(y, -0.5, 0.5), " ",
          lerp(y, y, half));                                           // 0.75 1.0 0.75 0.75 0.5 0.75
    print(degrees(radians(x)), " ", fract(x), " ", inverse_lerp(0.0, 4.0, x), " ",
          smoothstep(-1.0, 1.0, x), " ", wrap_angle(x));               // -2.0 0.0 -0.5 0.0 -2.0
    print(degrees(radians(y)), " ", fract(y), " ", inverse_lerp(y, y + 1.0, y), " ",
          smoothstep(y, y + 1.0, y), " ", wrap_angle(y));              // 0.75 0.75 0.0 0.0 0.75
    print(is_nan(x), " ", is_inf(y), " ", is_finite(x), " ", is_finite(y));   // false false true true
}

checkfree fn ints(xs: const i64[:]) {
    for x in xs {
        print(abs(x), " ", sign(x), " ", rem(x, 3), " ", div_euclid(x, 3), " ", clamp(x, -1, 1), " ",
              next_pow2(x));
    }
    var a: i32 = 1;
    var b: i32 = 2;
    swap(a, b);
    let small: u8 = 200;
    let neg: i8 = -3;
    print(min(xs), " ", max(xs), " ", a, " ", b, " ", max(small, 7), " ", min(abs(neg), 2));
}

checkfree fn hashes(n: i64, u: u64) {
    print(hash(n as! i8) == hash(n), " ", hash(n as! i16) == hash(n), " ", hash(n as! i32) == hash(n),
          " ", hash(u as! u8) == hash(u), " ", hash(u as! u16) == hash(u), " ",
          hash(u as! u32) == hash(u));                                 // true true true true true true
}

checkfree fn bits(x: u64, y: u32, n: i64) {
    print(popcount(x), " ", clz(x), " ", ctz(x), " ", rotl(y, 3), " ", rotr(x, 5), " ",
          byte_swap(y as u16), " ", is_pow2(n), " ", ilog2(n), " ", isqrt(n), " ", gcd(n, 12), " ",
          lcm(n, 6));
}

checkfree fn ids(i: NodeId, j: NodeId) -> i64 {
    var a = i;
    var b = j;
    swap(a, b);
    return nodes[min(a, b)].x + nodes[max(a, b)].x + nodes[clamp(i, a, b)].x;
}

checkfree fn vectors(v: double3, w: float2) {
    let n = normalize(v);
    let c = cross(v, double3 { 0.0, 0.0, 1.0 });
    let l = lerp(xy(v), double2 { 0.0, 0.0 }, 0.5);
    print(dot(n, n), " ", length_sq(c), " ", distance(v, v), " ", length(l), " ", dot(w, w), " ",
          length(w));                                                  // 1.0 25.0 0.0 2.5 25.0 5.0
    let f = float4 { 1.0, 2.0, 2.0, 0.0 };
    print(length(normalize(double2 { 0.0, 2.0 })), " ", length(normalize(f)), " ", distance(f, f),
          " ", lerp(f, f * 3.0, 0.5).x);                               // 1.0 1.0 0.0 2.0
}

checkfree fn transforms(m: float4x4, q: doubleq) {
    let p = transform_point(mul(inverse(m), m), float3 { 1.0, 2.0, 3.0 });
    let r = quat_cast(mat3_cast(slerp(q, doubleq_identity, 0.5)));
    print(p.x, " ", determinant(transpose(m)), " ", length_sq(r) > 0.999);   // 1.0 8.0 true
}

fn main() {
    floats(-2.0, 0.75);
    ints([7, -4]);    // 7 1 1 2 1 8, then 4 -1 -1 -2 -1 1, then -4 7 2 1 200 2
    hashes(-3, 200);
    bits(40, 0x12345678, 64);   // 2 58 3 2443359168 4611686018427387905 30806 true 6 8 4 192
    nodes.push(P { 10 });
    nodes.push(P { 20 });
    print(ids(1 as NodeId, 0 as NodeId));   // 10 + 20 + 20: 50
    vectors(double3 { 3.0, 4.0, 0.0 }, float2 { 3.0, 4.0 });
    transforms(scale(float3 { 2.0, 2.0, 2.0 }), doubleq { 0.0, 0.0, 1.0, 0.0 });
}
