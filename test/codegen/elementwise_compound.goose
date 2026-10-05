// runtime-debug: compound arithmetic uses the ordinary elementwise rules.
import vec;

// A vector is an ordinary struct; no stdlib overload is needed for +=.
fn vectors() {
    var position = float3_0;
    let velocity = float3_1;
    position += velocity;
    position -= float3 { 0.5, 0.25, 0.0 };
    position *= float3 { 4.0, 4.0, 4.0 };
    position /= float3 { 2.0, 2.0, 2.0 };
    position %= float3 { 0.75, 1.0, 1.5 };
    assert(position == float3 { 0.25, 0.5, 0.5 });

    var wide = double2 { -7.5, 7.5 };
    wide %= double2 { 2.0, -2.0 };
    assert(wide == double2 { -1.5, 1.5 });

    // Unsigned leaves still wrap at their own width, including in debug.
    var bytes: u8[2] = [255, 2];
    let add: u8[2] = [2, 4];
    bytes += add;
    let subtract: u8[2] = [2, 2];
    bytes -= subtract;
    let multiply: u8[2] = [2, 3];
    bytes *= multiply;
    let divide: u8[2] = [2, 2];
    bytes /= divide;
    let modulus: u8[2] = [5, 4];
    bytes %= modulus;
    assert(bytes[0] == 2 && bytes[1] == 2);

    var integers: i32[2] = [-7, 7];
    let divisors: i32[2] = [3, -3];
    integers %= divisors;
    assert(integers[0] == 2 && integers[1] == 1);
}

// Nested fixed arrays/structs compose, and their leaves can have different
// widths while staying uniformly integer or uniformly floating-point.
struct Floats { small: f32[2], wide: double2 }
struct Integers { small: i8, pad, wide: u32[2] }
fn aggregates() {
    var a = Floats { [1.0, 2.0], double2 { 3.0, 4.0 } };
    let b = Floats { [2.0, 3.0], double2 { 4.0, 5.0 } };
    a += b;
    assert(a == Floats { [3.0, 5.0], double2 { 7.0, 9.0 } });
    var rows = [a, b];
    rows *= [b, b];
    assert(rows[0] == Floats { [6.0, 15.0], double2 { 28.0, 45.0 } });
    var ints = Integers { 12, [20, 30] };
    ints /= Integers { 3, [4, 5] };
    assert(ints.small == 4 && ints.wide[0] == 5 && ints.wide[1] == 6);

    // Const contents do not prevent replacing a var as a whole (§9.5).
    var whole: const i64[2] = [1, 2];
    whole += [3, 4];
    assert(whole == [4, 6]);
}

struct Body { position: float3, tag: i64 }
fn evaluation_order() {
    var bodies = [Body { float3_1, 10 }, Body { float3_0, 20 }];
    var calls = 0;
    fn index() -> i64 { calls += 1; return 0; }
    fn change() -> float3 {
        assert(calls == 1);
        bodies[0].position = float3 { 20.0, 30.0, 40.0 };
        return float3 { 2.0, 3.0, 4.0 };
    }
    bodies[index()].position += change();
    assert(calls == 1 && bodies[0].position == float3 { 3.0, 4.0, 5.0 });
    assert(bodies[0].tag == 10 && bodies[1].tag == 20);

    // A receiver returning a reference also runs once. The RHS returns a
    // reference to the location it mutates: the old LHS must survive it.
    fn receiver() -> Body& { calls += 1; return bodies[0]; }
    fn change_ref() -> float3& {
        bodies[0].position = float3 { 10.0, 20.0, 30.0 };
        return bodies[0].position;
    }
    receiver().position += change_ref();
    assert(calls == 2 && bodies[0].position == float3 { 13.0, 24.0, 35.0 });

    // The saved destination remains the first vector if the RHS rebinds
    // the reference used to reach it.
    var first = float3_1;
    var second = float3 { 10.0, 20.0, 30.0 };
    var target .= first;
    fn rebind() -> float3 { target .= second; return float3 { 2.0, 3.0, 4.0 }; }
    target *= rebind();
    assert(first == float3 { 2.0, 3.0, 4.0 });
    assert(second == float3 { 10.0, 20.0, 30.0 } && target .== &second);

    let other: const float3& = second;
    target += other;
    assert(second == float3 { 20.0, 40.0, 60.0 });

    // Fixed values inside growable storage take the same update path.
    var growing: float3[>..<] = [float3_1];
    growing[0] += velocity();
    assert(growing[0] == float3 { 3.0, 4.0, 5.0 });
}
fn velocity() -> float3 { float3 { 2.0, 3.0, 4.0 } }

fn large_snapshots() {
    // >4 KiB operands use data-stack snapshots, including a nested RHS
    // update. Repeated statements must release those temporary slots.
    var values: i64[8192] = [0; 8192];
    var step: i64[8192] = [0; 8192];
    for i in values.len {
        values[i] = i;
        step[i] = i % 3 + 1;
    }
    fn change() -> i64[8192]& { values += step; return values; }
    values += change();
    assert(values[0] == 1 && values[8191] == 16384);
    for i in 2200 { values += step; }
    assert(values[0] == 2201 && values[8191] == 20784);
}

fn main() {
    vectors();
    aggregates();
    evaluation_order();
    large_snapshots();
    print("elementwise compound arithmetic passed");
}
