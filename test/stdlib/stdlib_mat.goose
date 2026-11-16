// stdlib/mat.goose: matrices as arrays of columns, and the 2D and 3D
// transforms. Results that go through sin and cos print rounded to 3 places.
import math;
import vec;
import mat;

fn r(x: f64) -> f64 { round(x * 1000.0) / 1000.0 + 0.0 }
fn r(x: f32) -> f64 { r(x as f64) }

fn show(v: double2) { print(r(v.x), " ", r(v.y)); }
fn show(v: double3) { print(r(v.x), " ", r(v.y), " ", r(v.z)); }
fn show(v: double4) { print(r(v.x), " ", r(v.y), " ", r(v.z), " ", r(v.w)); }
fn show(m: double4x4) { for i in 0..4 { show(row(m, i)); } }

// checkfree: what it calls of the module must keep no bounds check, so checkfree
// code may call it too (spec §7.14).
checkfree fn main() {
    // Columns: m[i] is column i, and elementwise + - * by a scalar are the language's.
    let a: double2x2 = [double2 { 1.0, 3.0 }, double2 { 2.0, 4.0 }];   // rows (1 2) and (3 4)
    print(a[1].x, " ", row(a, 1).x, " ", determinant(a));            // 2.0 3.0 -2.0
    let v2 = double2 { 1.0, 1.0 };
    show(mul(a, v2));                                                // 3.0 7.0
    show(mul(v2, a));                                                // 4.0 6.0
    let aa = mul(a, a);
    print(aa[0].x, " ", aa[1].x, " ", aa[0].y, " ", aa[1].y);        // 7.0 10.0 15.0 22.0
    let ai = mul(a, inverse(a));
    show(ai[0]);                                                     // 1.0 0.0
    show(ai[1]);                                                     // 0.0 1.0
    let el = a * a;                                                  // elementwise, not mul
    let sc = a + a * 2.0;
    print(el[1].y, " ", sc[1].y, " ", transpose(a)[1].x);            // 16.0 12.0 3.0
    let op = outer_product(double2 { 1.0, 2.0 }, double2 { 3.0, 4.0 });
    print(op[0].x, " ", op[1].x, " ", op[0].y, " ", op[1].y);        // 3.0 4.0 6.0 8.0

    let b: double3x3 = [double3 { 2.0, 0.0, 1.0 }, double3 { 1.0, 3.0, 0.0 }, double3 { 0.0, 1.0, 4.0 }];
    print(determinant(b), " ", determinant(double3x3_identity));     // 25.0 1.0
    let bi = mul(inverse(b), b);
    show(bi[0]);                                                     // 1.0 0.0 0.0
    show(bi[1]);                                                     // 0.0 1.0 0.0
    show(bi[2]);                                                     // 0.0 0.0 1.0
    show(mul(b, double3 { 1.0, 2.0, 3.0 }));                         // 4.0 9.0 13.0
    show(mul(double3 { 1.0, 2.0, 3.0 }, b));                         // 5.0 7.0 14.0
    let op3 = outer_product(double3 { 1.0, 2.0, 3.0 }, double3 { 0.0, 1.0, 2.0 });
    print(op3[0].z, " ", op3[2].y, " ", op3[1].z);                   // 0.0 4.0 3.0

    // A 4x4 with a full bottom row, so every cofactor matters.
    let c: double4x4 = [double4 { 1.0, 2.0, 0.0, 1.0 }, double4 { 0.0, 1.0, 3.0, 0.0 },
                        double4 { 2.0, 0.0, 1.0, 1.0 }, double4 { 1.0, 1.0, 0.0, 2.0 }];
    print(determinant(c), " ", determinant(transpose(c)));           // 16.0 16.0
    show(mul(c, inverse(c)));                                        // the identity, by rows
    show(mul(inverse(c), c));                                        // and again
    show(mul(c, double4 { 1.0, 2.0, 3.0, 4.0 }));                    // 11.0 8.0 9.0 12.0
    show(mul(double4 { 1.0, 2.0, 3.0, 4.0 }, c));                    // 9.0 11.0 9.0 11.0
    let op4 = outer_product(double4 { 1.0, 2.0, 3.0, 4.0 }, double4 { 1.0, 0.0, 0.0, 2.0 });
    show(op4[3]);                                                    // 2.0 4.0 6.0 8.0
    print(op4[0].z, " ", op4[1].x);                                  // 3.0 0.0
    print(determinant(double4x4_identity), " ", float4x4_identity[3].w);   // 1.0 1.0
    let c3 = to3x3(c);
    let c4 = to4x4(c3);
    print(c3[2].x, " ", c4[2].x, " ", c4[3].w, " ", c4[0].w);        // 2.0 2.0 1.0 0.0
    let c2 = to2x2(c3);
    let c33 = to3x3(c2);
    print(c2[0].y, " ", c33[2].z, " ", c33[1].z);                    // 2.0 1.0 0.0

    // 3D transforms: scale, then rotate, then translate.
    let model = mul(translate(double3 { 10.0, 0.0, 0.0 }),
                    mul(rotate_z(PI / 2.0), scale(double3 { 2.0, 2.0, 2.0 })));
    show(transform_point(model, double3 { 1.0, 0.0, 0.0 }));         // 10.0 2.0 0.0
    show(transform_vector(model, double3 { 1.0, 0.0, 0.0 }));        // 0.0 2.0 0.0
    show(transform_point(inverse(model), double3 { 10.0, 2.0, 0.0 }));   // 1.0 0.0 0.0
    show(row(rotate(double3 { 0.0, 0.0, 3.0 }, PI / 2.0), 0));       // 0.0 -1.0 0.0 0.0
    show(row(rotate_z(PI / 2.0), 0));                                // 0.0 -1.0 0.0 0.0
    show(transform_vector(rotate_x(PI / 2.0), double3 { 0.0, 1.0, 0.0 }));   // 0.0 0.0 1.0
    show(transform_vector(rotate_y(PI / 2.0), double3 { 0.0, 0.0, 1.0 }));   // 1.0 0.0 0.0
    show(transform_vector(rotate(double3 { 1.0, 1.0, 1.0 }, TAU / 3.0), double3 { 1.0, 0.0, 0.0 }));
    // 0.0 1.0 0.0

    // Cameras: the near and far planes at depths 0 and 1, y up.
    let proj = perspective(PI / 2.0, 2.0, 1.0, 10.0);
    show(transform_point(proj, double3 { 2.0, 1.0, -1.0 }));         // 1.0 1.0 0.0
    show(transform_point(proj, double3 { 0.0, 0.0, -10.0 }));        // 0.0 0.0 1.0
    let o = ortho(0.0, 800.0, 0.0, 600.0, 0.0, 1.0);
    let o2d = ortho(0.0, 800.0, 0.0, 600.0);                         // z 1 to -1, depth 0 to 1
    show(transform_point(o2d, double3 { 800.0, 600.0, 0.0 }));       // 1.0 1.0 0.5
    show(transform_point(o2d, double3 { 200.0, 150.0, 0.5 }));       // -0.5 -0.5 0.25
    show(transform_point(o2d, double3 { 0.0, 0.0, -1.0 }));          // -1.0 -1.0 1.0
    let o2 = ortho(-2.0, 2.0, -1.0, 1.0, 2.0, 10.0);
    show(transform_point(o2, double3 { 2.0, -1.0, -2.0 }));          // 1.0 -1.0 0.0
    show(transform_point(o2, double3 { -1.0, 0.5, -10.0 }));         // -0.5 0.5 1.0
    show(transform_point(o, double3 { 800.0, 0.0, -1.0 }));          // 1.0 -1.0 1.0
    let view = look_at(double3 { 0.0, 0.0, 5.0 }, double3 { 0.0, 0.0, 0.0 }, double3 { 0.0, 1.0, 0.0 });
    show(transform_point(view, double3 { 1.0, 2.0, 0.0 }));          // 1.0 2.0 -5.0
    let side = look_at(double3 { 5.0, 0.0, 0.0 }, double3 { 0.0, 0.0, 0.0 }, double3 { 0.0, 1.0, 0.0 });
    show(transform_point(side, double3 { 0.0, 0.0, -1.0 }));         // 1.0 0.0 -5.0
    // An f32 argument makes the whole call f32.
    let fov:   f32      = 1.0;
    let fproj: float4x4 = perspective(fov, 1.5, 0.1, 100.0);
    print(r(fproj[1].y));                                            // 1.83

    // 2D transforms: homogeneous 3x3.
    let m2 = mul(translate(double2 { 5.0, 0.0 }), mul(rotate(PI / 2.0), scale(double2 { 3.0, 1.0 })));
    show(transform_point(m2, double2 { 1.0, 0.0 }));                 // 5.0 3.0
    show(transform_vector(m2, double2 { 1.0, 0.0 }));                // 0.0 3.0
    show(transform_point(inverse(m2), double2 { 5.0, 3.0 }));        // 1.0 0.0
}
