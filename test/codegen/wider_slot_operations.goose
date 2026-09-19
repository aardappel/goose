// A cast, `~` and unary `-` compute at their own type (§6.1–6.3) wherever
// their value lands: stored into a wider slot, or widened as the operand of a
// wider operation, an i8 cast still wraps at i8 and ~ on a u8 still flips 8
// bits. Release semantics throughout: the checked `as` and the `-` below
// change their value, which a GS_DEBUG build aborts on.
struct VF { a: varint, b: varint }
struct Wide { a: i64, b: u16 }

fn wide(x: i64) -> i64 { x }
fn narrowed(a: i64) -> i64 { a as! i8 }
fn flipped(c: u8) -> u32 { ~c }

fn main() {
    // Read from arrays, so no value is known to the optimizer.
    var ints: i64[>..] = [300, 16777217];
    var bytes: u8[>..] = [15];
    var smalls: i8[>..] = [-128];
    var floats: f64[>..] = [0.1, 300.7];
    let a = ints[0];
    let big = ints[1];
    let c = bytes[0];
    let m = smalls[0];
    let x = floats[0];
    let fx = floats[1];

    // Into wider variables.
    let y: i64 = a as! i8;          // 44
    print(y);
    let y2: i32 = a as! u8;         // 44
    print(y2);
    let y3: u64 = a as! u8;         // 44
    print(y3);
    let y4: i64 = a as i8;          // 44
    print(y4);
    let y5: i64 = fx as! i8;        // 44
    print(y5);
    let y6: i64 = fx as i8;         // 44
    print(y6);
    let z: u16 = ~c;                // 240
    print(z);
    let z2: i64 = ~c;               // 240
    print(z2);
    let n: i64 = -m;                // -128
    print(n);
    let d: f64 = x as! f32;         // 0.10000000149011612
    print(d);
    let d2: f64 = x as f32;         // 0.10000000149011612
    print(d2);
    let d3: f64 = big as! f32;      // 16777216
    print(d3);
    let d4: f64 = big as f32;       // 16777216
    print(d4);
    var w: i64 = 0;
    w = a as! i8;                   // 44
    print(w);
    w = ~c;                         // 240
    print(w);

    // Into fields, elements, arguments and results.
    let s = VF { a: a as! i8, b: ~c };      // 44 240
    print(s.a, " ", s.b);
    let t = Wide { a: a as! i8, b: ~c };    // 44 240
    print(t.a, " ", t.b);
    let arr: i64[3] = [a as! i8, ~c, -m];   // [44, 240, -128]
    print(arr);
    let vs: varint[] = [a as! i8, ~c, -m];  // [44, 240, -128]
    print(vs);
    var grown: varint[>..] = [];
    grown.push(a as! i8);
    grown.push(~c);                         // [44, 240]
    print(grown);
    print(wide(a as! i8), " ", wide(~c));   // 44 240
    print(narrowed(a), " ", flipped(c));    // 44 240

    // As the operand of a wider operation.
    print((a as! i8) + big);                // 16777261
    print((a as! i8) == a - 256);           // true
    print(~c + (c as u16));                 // 255
    print((x as! f32) == d);                // true

    // Constants, which the optimizer folds.
    let k: i64 = 300 as! i8;                // 44
    print(k);
    let k2: u16 = ~(15 as u8);              // 240
    print(k2);
    let k3: f64 = 0.1 as! f32;              // 0.10000000149011612
    print(k3);
    let k4: f64 = 16777217 as! f32;         // 16777216
    print(k4);
    let mc: i8 = -128;
    let k5: i64 = -mc;                      // -128
    print(k5);
    let k6 = VF { a: 300 as! i8, b: ~(15 as u8) };   // 44 240
    print(k6.a, " ", k6.b);
    let k7: varint[] = [300 as! i8, ~(15 as u8)];    // [44, 240]
    print(k7);
}
