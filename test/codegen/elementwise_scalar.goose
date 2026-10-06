// runtime-debug: scalars broadcast once, at the aggregate's leaf width.
import vec;

struct Nested { first: f32, pad, rows: float2[2], last: f32 }
struct Pair<T> { x: T, y: T }
struct ReadOnly { let x: f32, y: f32 }

fn widths() {
    let v = float3 { 1.0, 2.0, 4.0 };
    var scale: f32 = 2.0;
    assert(v * scale == float3 { 2.0, 4.0, 8.0 });
    assert(scale * v == float3 { 2.0, 4.0, 8.0 });
    assert(v / scale == float3 { 0.5, 1.0, 2.0 });
    assert(scale / v == float3 { 2.0, 1.0, 0.5 });
    assert(v * 2 == v * 2.0);
    assert(v * (0.5 + 0.5) == v);
    assert(v * (if scale > 0.0 { 2.0 } else { 3.0 }) == v * scale);
    assert(v / (if scale > 0.0 { 2 } else { 3 }) == v / scale);
    let other: const float3& = v;
    let factor: const f32& = scale;
    assert(other * factor == scale * other);

    let d = double2 { 1.0, 4.0 };
    var wide: f64 = 2.0;
    assert(d * scale == double2 { 2.0, 8.0 });
    assert(wide / d == double2 { 2.0, 0.5 });
    var count: i64 = 2;
    assert(v * count == float3 { 2.0, 4.0, 8.0 });

    // Every signed/unsigned width keeps its representation. Smaller typed
    // operands may widen, but the nominal aggregate never changes type.
    fn integer<T>(a: Pair<T>, s: T) {
        let p: Pair<T> = a * s;
        assert(p.x == 12 && p.y == 24);
        assert(s * a == p);
        assert(p / s == a);
        assert(24 / a == Pair<T> { 4, 2 });
        var q = a;
        q *= s;
        q /= s;
        assert(q == a);
    }
    integer(Pair<i8> { 6, 12 }, 2);
    integer(Pair<u8> { 6, 12 }, 2);
    integer(Pair<i16> { 6, 12 }, 2);
    integer(Pair<u16> { 6, 12 }, 2);
    integer(Pair<i32> { 6, 12 }, 2);
    integer(Pair<u32> { 6, 12 }, 2);
    integer(Pair<i64> { 6, 12 }, 2);
    integer(Pair<u64> { 6, 12 }, 2);
    var small: i8 = 2;
    assert(int2 { 3, 4 } * small == int2 { 6, 8 });
    assert(Pair<u8> { 255, 128 } * 2 == Pair<u8> { 254, 0 });
    assert(Pair<i16> { -7, 7 } / 3 == Pair<i16> { -2, 2 });
}

fn aggregates() {
    var nested = Nested { 2.0, [float2 { 4.0, 6.0 }, float2 { 8.0, 10.0 }], 12.0 };
    let original = nested;
    nested *= 2;
    assert(nested.first == 4.0 && nested.rows[1].y == 20.0 && nested.last == 24.0);
    nested /= 2.0;
    assert(nested == original);
    let reciprocal = 24.0 / nested;
    assert(reciprocal.first == 12.0 && reciprocal.last == 2.0);
    var row: const i32[3] = [2, 4, 8];
    row *= 2;
    assert(row[0] == 4 && row[2] == 16);
    let rows: i32[3][2] = [[2, 4, 8], [4, 8, 16]];
    let half = rows / 2;
    assert(half[0][0] == 1 && half[1][2] == 8);

    // Padding and immutable fields are copied as part of the result.
    assert((ReadOnly { 3.0, 4.0 } * 2.0).x == 6.0);
}

fn evaluation_order() {
    var v = float3 { 2.0, 3.0, 4.0 };
    v = v * v.x;
    assert(v == float3 { 4.0, 6.0, 8.0 });
    v = v.x * v;
    assert(v == float3 { 16.0, 24.0, 32.0 });
    v /= v.x;
    assert(v == float3 { 1.0, 1.5, 2.0 });

    var calls = 0;
    fn scalar() -> f32 {
        calls += 1;
        v = float3 { 10.0, 20.0, 30.0 };
        return 2.0;
    }
    // The left aggregate is read before the right scalar changes it.
    let a = v * scalar();
    assert(calls == 1 && a == float3 { 2.0, 3.0, 4.0 });
    fn vector() -> float3 { calls += 1; return v; }
    let b = scalar() * vector();
    assert(calls == 3 && b == float3 { 20.0, 40.0, 60.0 });
    var s: f32 = 3.0;
    fn change_scalar() -> float3 { s = 5.0; return float3_1; }
    let c = s * change_scalar();
    assert(c == float3 { 3.0, 3.0, 3.0 } && s == 5.0);
    assert((s * change_scalar()).x == 5.0);

    var rows = [float3 { 2.0, 4.0, 6.0 }, float3_1];
    fn index() -> i64 { calls += 1; return 0; }
    fn change_row() -> f32 { rows[0] = float3_0; return 3.0; }
    rows[index()] *= change_row();
    assert(calls == 4 && rows[0] == float3 { 6.0, 12.0, 18.0 });

    var target .= rows[0];
    fn rebind() -> f32 { target .= rows[1]; return 2.0; }
    target /= rebind();
    assert(rows[0] == float3 { 3.0, 6.0, 9.0 } && rows[1] == float3_1);

    // Snapshot lifetimes also matter for large aggregates on data stacks.
    var large: f32[2048] = [2.0; 2048];
    fn change_large() -> f32 { large[0] = 99.0; return 2.0; }
    for i in 2200 {
        large = large / change_large();
        assert(large[0] == large[2047]);
    }
}

fn main() {
    widths();
    aggregates();
    evaluation_order();
    print("elementwise scalar arithmetic passed");
}
