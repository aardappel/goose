// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of b either way
// warning: the construct's value is a copy of b either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of b either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of k either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of b either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of p1 either way
// warning: the construct's value is a copy of a2 either way
// warning: the construct's value is a copy of s1 either way
// warning: the construct's value is a copy of sh2 either way
// warning: the construct's value is a copy of rr either way
// warning: the construct's value is a copy of rec.v either way
// warning: the construct's value is a copy of a either way
// warning: the construct's value is a copy of b either way
// A construct whose destination names no type makes its value a copy of
// the branch's (§4.1), and a reference a branch gives, `&x` included, is
// copied as its pointee (§3.8): each branch loads it, and neither a later
// write to the pointee nor a write to the copy reaches the other. The `&`
// is redundant there, and warns; at a typed destination it loads without a
// warning, as `&x` does wherever a reference meets a value, and a whole
// array's `&x` at a slice parameter views the array.
struct P { x: i64, y: i64 }
enum Shape { Circle { r: i64 }, Square { s: i64 } }
struct Rec { name: u8[], v: varint, w: varint }

fn inc(x: i64) -> i64 { x + 1 }
fn tens(x) -> i64 { x * 10 }
fn total(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn pick(c: bool, a: i64, b: i64) -> i64 { if c { &a } else { &b } }

fn main() {
    var a = 1;
    var b = 2;
    var k: i32 = 3;
    let c = a < b;
    let d = a > b;

    // Each construct, some branches without the &.
    let r1 = if c { &a } else { &b };
    let r2 = match a { 1 => &b, _ => a };
    let r3 = block { if d { break &a; } b };
    let r4 = loop { break &a; };
    let r5 = { &b };
    let r6 = if d { &a } else if c { b } else { &a };
    let r7 = block { if d { break a; } if c { break &k; } a };
    var w = if c { &a } else { b };
    w = 7;
    a = 5;
    print(r1, " ", r2, " ", r3, " ", r4, " ", r5, " ", r6, " ", r7, " ", w, " ", a);

    // Destinations that load the copy, and typed ones that load without a
    // warning.
    print(if c { &a } else { b });
    print((if c { &a } else { b }) + 1, " ", inc(if d { a } else { &b }),
          " ", tens(if c { &a } else { b }));
    let y: i64 = if d { &a } else { &b };
    print(y, " ", pick(c, a, b), " ", pick(d, a, b));

    // Pointees of other kinds.
    var p1 = P { x: 10, y: 20 };
    var p2 = P { x: 30, y: 40 };
    let q = if c { &p1 } else { p2 };
    p1.x = 11;
    var a1: i64[3] = [1, 2, 3];
    var a2: i64[3] = [4, 5, 6];
    let arr = if d { a1 } else { &a2 };
    a2[0] = 9;
    let s1 = a1[..];
    let s = if c { &s1 } else { a2[1..] };
    var sh1: Shape = Shape.Circle { r: 3 };
    var sh2: Shape = Shape.Square { s: 4 };
    let sh = if c { &sh2 } else { sh1 };
    sh2 = Shape.Circle { r: 8 };
    print(q.x, " ", q.y, " ", arr[0], " ", s.len);
    match sh { Circle x => print("circle ", x.r), Square x => print("square ", x.s) }
    let rr: i64& = b;
    let t = if c { &rr } else { a };
    b = 8;
    var tot = 0;
    for &e in a1 { tot += if c { &e } else { a }; }
    let rec = Rec { name: "ab", v: 300, w: 7 };
    print(t, " ", tot, " ", if c { &rec.v } else { rec.w });

    // A condition the optimizer folds leaves the branch's & in the
    // construct's place.
    let yes = true;
    let f1 = if yes { &a } else { b };
    let f2 = { &b };
    print(f1, " ", f2);

    // `&x` meeting a value outside a construct, and a whole array's meeting
    // a slice parameter.
    let z: i64 = &a;
    print(&a, " ", &a + b, " ", inc(&b), " ", z);
    let ra: i64[3]& = a2;
    print(total(&a1), " ", total(&ra));
}
