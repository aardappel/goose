// runtime-debug: unary minus negates float and signed integer aggregates
// memberwise (§6.1).
import vec;

struct Nested { first: f32, pad, rows: float2[2], last: f32 }
struct Ints { a: i8, b: i16[2], c: i32, d: i64 }
struct Body { pos: double3, vel: f64[3] }

fn made(x: f32) -> float3 { return float3 { x, 2.0, -3.0 }; }

fn main() {
    let v = float3 { 1.0, -2.0, 0.0 };
    let n = -v;
    assert(n == float3 { -1.0, 2.0, 0.0 });
    print(n.x, " ", n.y, " ", n.z);
    // A float member negates exactly: 0.0 becomes -0.0, which `0.0 - x`
    // would not give.
    print(1.0 / n.z);
    assert(-(-v) == v);
    assert(-v + v == float3_0);
    assert(-made(4.0) == float3 { -4.0, -2.0, 3.0 });
    print(-[1.5, -2.5, 3.5]);

    let i = Ints { a: -128 + 1, b: [32767, -5], c: 7, d: -9 };
    let j = -i;
    print(j.a, " ", j.b, " ", j.c, " ", j.d);
    assert(-j == i);
    print(-int3 { 1, -2, 3 } * 2);

    // Each member reads the operand before the same member of the
    // destination is written, so a value negates in place.
    var m = Nested { first: 1.0, rows: [float2 { 2.0, -3.0 }, float2 { 4.0, 5.0 }], last: -6.0 };
    m = -m;
    print(m.first, " ", m.rows[0].x, " ", m.rows[0].y, " ", m.rows[1].x, " ", m.rows[1].y,
          " ", m.last);
    m.rows[1] = -m.rows[1];
    print(m.rows[1].x, " ", m.rows[1].y);

    var bodies: Body[2] = [Body { pos: double3 { 1.0, 2.0, 3.0 }, vel: [0.5, -0.5, 1.5] },
                           Body { pos: double3_0, vel: [1.0, 1.0, 1.0] }];
    bodies[1] = -bodies[0];
    print(bodies[1].pos.z, " ", bodies[1].vel);
    bodies[0].vel = -bodies[0].vel * 2.0;
    print(bodies[0].vel);
    var k = 0;
    let pick = -bodies[{ k += 1; k }].pos;
    print(k, " ", pick.x);
}
