// rotation(from, to) of nearly opposite f32 directions takes `from` onto
// `to`, the short way round. w was a . h, which near opposite is mostly a's
// and b's rounding off unit length: these two missed by 4 degrees, the long
// way round (w < 0), and by 22.
import math;
import vec;
import quat;

fn r(x: f32) -> f64 { round((x as f64) * 1000.0) / 1000.0 + 0.0 }

fn check(from: float3, to: float3) {
    let q = rotation(from, to);
    let got = rotate(q, normalize(from));
    let want = normalize(to);
    print(r(got.x), " ", r(got.y), " ", r(got.z));
    print(r(want.x), " ", r(want.y), " ", r(want.z));
    print(length(got - want) < 0.00001, " ", q.w >= 0.0);
}

fn main() {
    // 1.6e-6 and 1.1e-6 rad from opposite; the second missed by the most
    // of a million random pairs.
    check(float3 { 1.0, 2.0, 3.0 }, float3 { -1.0, -2.0, -3.00001 });
    check(float3 { -1.013356, -1.7408707, 0.15774797 }, float3 { 0.15095916, 0.25933626, -0.023499375 });
}
