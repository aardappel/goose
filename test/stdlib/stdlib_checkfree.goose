// std's scalar helpers, math's and vec's functions are checkfree, so a
// checkfree function can call them (spec §7.14) at every type they take.
import std;
import math;
import vec;

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
}

fn main() {
    floats(-2.0, 0.75);
    ints([7, -4]);    // 7 1 1 2 1 8, then 4 -1 -1 -2 -1 1, then -4 7 2 1 200 2
    nodes.push(P { 10 });
    nodes.push(P { 20 });
    print(ids(1 as NodeId, 0 as NodeId));   // 10 + 20 + 20: 50
    vectors(double3 { 3.0, 4.0, 0.0 }, float2 { 3.0, 4.0 });
}
