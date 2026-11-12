// stdlib/quat.goose: quaternions, and their conversions to and from
// matrices. Results print rounded to 3 places.
import math;
import vec;
import mat;
import quat;

fn r(x: f64) -> f64 { round(x * 1000.0) / 1000.0 + 0.0 }
fn r(x: f32) -> f64 { r(x as f64) }

fn show(v: double3) { print(r(v.x), " ", r(v.y), " ", r(v.z)); }
fn show(v: float3) { print(r(v.x), " ", r(v.y), " ", r(v.z)); }
fn show(q: doubleq) { print(r(q.x), " ", r(q.y), " ", r(q.z), " ", r(q.w)); }
fn show(q: floatq) { print(r(q.x), " ", r(q.y), " ", r(q.z), " ", r(q.w)); }

// The same rotation: q or -q.
fn same(a: doubleq, b: doubleq) -> bool {
    let d = dot(a, b);
    d * d > 0.999999
}
fn same(a: floatq, b: floatq) -> bool {
    let d = dot(a, b);
    d * d > 0.999999
}

fn main() {
    let qz = angle_axis(PI / 2.0, double3 { 0.0, 0.0, 2.0 });
    show(qz);                                                        // 0.0 0.0 0.707 0.707
    show(rotate(qz, double3 { 1.0, 0.0, 0.0 }));                     // 0.0 1.0 0.0
    show(mul(mat3_cast(qz), double3 { 1.0, 0.0, 0.0 }));             // 0.0 1.0 0.0
    let qx = angle_axis(PI / 2.0, double3 { 1.0, 0.0, 0.0 });
    let both = mul(qz, qx);                                          // qx, then qz
    show(rotate(both, double3 { 0.0, 1.0, 0.0 }));                   // 0.0 0.0 1.0
    show(rotate(both, double3 { 0.0, 0.0, 1.0 }));                   // 1.0 0.0 0.0
    show(transform_vector(mul(mat4_cast(qz), mat4_cast(qx)), double3 { 0.0, 0.0, 1.0 }));
    // 1.0 0.0 0.0
    show(rotate(inverse(qz), double3 { 0.0, 1.0, 0.0 }));            // 1.0 0.0 0.0
    show(conjugate(qz));                                             // 0.0 0.0 -0.707 0.707
    show(mul(qz, inverse(qz)));                                      // 0.0 0.0 0.0 1.0
    print(r(length(qz)), " ", r(length_sq(both)), " ", r(dot(qz, doubleq_identity)));
    // 1.0 1.0 0.707
    show(normalize(doubleq { 0.0, 0.0, 3.0, 4.0 }));                 // 0.0 0.0 0.6 0.8
    let an, ax = to_angle_axis(qz);
    print(r(an), " ", r(ax.x), " ", r(ax.y), " ", r(ax.z));         // 1.571 0.0 0.0 1.0
    let an0, ax0 = to_angle_axis(doubleq_identity);
    print(an0, " ", ax0.x);                                          // 0.0 1.0

    // To and from matrices: a matrix gives q or -q, the same rotation.
    show(quat_cast(mat3_cast(both)));                                // 0.5 0.5 0.5 0.5
    let back = quat_cast(mat4_cast(angle_axis(3.0, double3 { 1.0, 2.0, 2.0 })));
    show(back);                                                      // 0.332 0.665 0.665 0.071
    let flip = quat_cast(mat3_cast(angle_axis(PI, double3 { 0.0, 1.0, 0.0 })));
    show(flip);                                                      // 0.0 1.0 0.0 0.0
    let flipz = quat_cast(mat3_cast(angle_axis(PI, double3 { 0.0, 0.0, 1.0 })));
    show(flipz);                                                     // 0.0 0.0 1.0 0.0
    let flipx = quat_cast(mat3_cast(angle_axis(PI, double3 { 1.0, 0.0, 0.0 })));
    show(flipx);                                                     // 1.0 0.0 0.0 0.0
    let qxb = angle_axis(2.5, double3 { 1.0, 0.2, 0.1 });           // x the largest
    show(qxb);                                                       // 0.926 0.185 0.093 0.315
    show(quat_cast(mat3_cast(qxb)));                                 // the same

    // Interpolation: the ends, the middle, and the shorter way round.
    let q0 = doubleq_identity;
    show(slerp(q0, qz, 0.0));                                        // 0.0 0.0 0.0 1.0
    show(slerp(q0, qz, 1.0));                                        // 0.0 0.0 0.707 0.707
    show(rotate(slerp(q0, qz, 0.5), double3 { 1.0, 0.0, 0.0 }));     // 0.707 0.707 0.0
    show(rotate(slerp(q0, -qz, 0.5), double3 { 1.0, 0.0, 0.0 }));    // 0.707 0.707 0.0
    show(rotate(nlerp(q0, qz, 0.5), double3 { 1.0, 0.0, 0.0 }));     // 0.707 0.707 0.0
    show(slerp(qz, qz, 0.3));                                        // 0.0 0.0 0.707 0.707
    // At a quarter, where slerp's constant speed and nlerp's chord differ.
    let big = angle_axis(2.0, double3 { 0.0, 0.0, 1.0 });
    let sa, sax = to_angle_axis(slerp(q0, big, 0.25));
    let na, nax = to_angle_axis(nlerp(q0, big, 0.25));
    print(r(sa), " ", r(na), " ", r(sax.z), " ", r(nax.z));          // 0.5 0.467 1.0 1.0

    // Rotations between directions.
    show(rotate(rotation(double3 { 1.0, 0.0, 0.0 }, double3 { 0.0, 0.0, 3.0 }), double3 { 2.0, 0.0, 0.0 }));
    // 0.0 0.0 2.0
    show(rotation(double3 { 1.0, 0.0, 0.0 }, double3 { 1.0, 0.0, 0.0 }));   // 0.0 0.0 0.0 1.0
    show(rotate(rotation(double3 { 0.0, 1.0, 0.0 }, double3 { 0.0, -1.0, 0.0 }), double3 { 0.0, 1.0, 0.0 }));
    // 0.0 -1.0 0.0
    show(rotate(rotation(double3 { 0.0, 0.0, 1.0 }, double3 { 0.0, 0.0, -1.0 }), double3 { 0.0, 0.0, 1.0 }));
    // 0.0 0.0 -1.0
    let look = quat_look_at(double3 { 1.0, 0.0, 0.0 }, double3 { 0.0, 1.0, 0.0 });
    show(rotate(look, double3 { 0.0, 0.0, -1.0 }));                  // 1.0 0.0 0.0
    show(rotate(look, double3 { 0.0, 1.0, 0.0 }));                   // 0.0 1.0 0.0

    // compose: scale, then rotate, then move, as the products do.
    let cq = angle_axis(0.7, double3 { 1.0, 2.0, 0.5 });
    let ct = double3 { 5.0, -3.0, 2.0 };
    let cs = double3 { 2.0, 0.5, 3.0 };
    let cm = compose(ct, cq, cs);
    let chain = mul(translate(ct), mul(mat4_cast(cq), scale(cs)));
    let cp = double3 { 1.0, 1.0, 1.0 };
    show(transform_point(cm, cp) - transform_point(chain, cp));      // 0.0 0.0 0.0
    show(transform_point(cm, cp) - (rotate(cq, cp * cs) + ct));      // 0.0 0.0 0.0
    show(transform_point(compose(ct, doubleq_identity, cs), cp));    // 7.0 -2.5 5.0
    print(cm[3].x, " ", cm[3].w, " ", cm[0].w);                      // 5.0 1.0 0.0

    // Small angles and nearly opposite directions keep their precision in f32.
    let small, smax = to_angle_axis(angle_axis(0.001, float3 { 0.0, 0.0, 1.0 }));
    print(r(small * 1000.0), " ", r(length(smax)));                  // 1.0 1.0
    let tiny, tax = to_angle_axis(angle_axis(0.0001, float3 { 0.0, 1.0, 0.0 }));
    print(r(tiny * 10000.0), " ", r(tax.y));                         // 1.0 1.0
    let near = rotation(float3 { 1.0, 0.0, 0.0 }, normalize(float3 { -1.0, 0.002, 0.0 }));
    print(r(length(near)));                                          // 1.0
    show(rotate(near, float3 { 1.0, 0.0, 0.0 }));                    // -1.0 0.002 0.0
    show(rotate(rotation(double3 { 1.0, 0.0, 0.0 }, double3 { 1.0, 0.0001, 0.0 }), double3 { 1000.0, 0.0, 0.0 }));
    // 1000.0 0.1 0.0

    // Euler angles: x, then y, then z.
    let e = double3 { 0.3, -0.5, 1.2 };
    show(euler_angles(from_euler(e)));                               // 0.3 -0.5 1.2
    let ez = from_euler(double3 { 0.0, 0.0, PI / 2.0 });
    show(ez);                                                        // 0.0 0.0 0.707 0.707
    show(rotate(from_euler(e), double3 { 1.0, 2.0, 3.0 }));
    show(rotate(mul(angle_axis(1.2, double3 { 0.0, 0.0, 1.0 }),
                    mul(angle_axis(-0.5, double3 { 0.0, 1.0, 0.0 }), angle_axis(0.3, double3 { 1.0, 0.0, 0.0 }))),
                double3 { 1.0, 2.0, 3.0 }));                         // the same
    // At a yaw of +-PI/2 the angles still rebuild the rotation: true each time.
    for e in [double3 { 0.3, PI / 2.0, 0.2 }, double3 { 0.0, PI / 2.0, 0.7 }, double3 { 1.0, -PI / 2.0, -2.0 },
              double3 { 0.3, 1.5707, 0.2 }, double3 { 3.0, 1.0, -3.0 }] {
        let q = from_euler(e);
        print(same(q, from_euler(euler_angles(q))), " ", r(euler_angles(q).y));
    }                                                                // with yaws 1.571 1.571 -1.571 1.571 1.0
    for e in [float3 { 0.3, 1.5707964, 0.2 }, float3 { 1.0, -1.5707964, -2.0 }, float3 { 0.3, 1.5703, 0.2 }] {
        let q = from_euler(e);
        print(same(q, from_euler(euler_angles(q))));
    }

    // f32 quaternions.
    let fq = angle_axis(radians(90.0) as f32, float3 { 0.0, 1.0, 0.0 });
    show(fq);                                                        // 0.0 0.707 0.0 0.707
    show(rotate(fq, float3 { 0.0, 0.0, 1.0 }));                      // 1.0 0.0 0.0
    show(floatq_identity);                                           // 0.0 0.0 0.0 1.0
}
