// An integer converts to a float wherever a float is expected (§6.3), and an
// expression whose float-ness comes only from float literals and integers
// adapts to its destination as a literal does: an f32 where an f32 is
// expected, computed in 32 bits throughout, and an f64 where nothing gives it
// a type. Nine tenths show the width: 9 * 0.1 is 0.9 at f64, and
// 0.90000004 computed at f32 (were it computed at f64 and only then rounded
// to f32, it would print as 0.9).
import std;

struct Ent { x: i32, w: i32 }
struct V2 { x: f32, y: f32 }
struct Packed { n: varint }

fn id32(x: f32) -> f32 { x }
fn id64(x: f64) -> f64 { x }
fn wider(n: i32) -> f64 { n }
fn dflt(x: f32 = 1) -> f32 { x }
fn next(n: i32) -> i32 { n + 1 }

// An integer argument converts to a float only where nothing else takes the
// call (§7.1).
fn pick(x: i32) -> u8[:] { "i32" }
fn pick(x: f64) -> u8[:] { "f64" }
fn wide(x: i64) -> u8[:] { "i64" }
fn wide(x: f32) -> u8[:] { "f32" }
fn prec(x: f32) -> u8[:] { "f32" }
fn prec(x: f64) -> u8[:] { "f64" }
fn str(x: f32) -> u8[:] { "user str" }

enum Shape { Circle { r: f64 }, Square { s: f64 } }
fn area(c: Shape.Circle, k: f64) -> f64 { c.r * c.r * 3 * k }
fn area(s: Shape.Square, k: f64) -> f64 { s.s * s.s * k }
fn scale(s: Shape, k: f32) -> u8[:] { "whole" }
fn scale(c: Shape.Circle, k: i64) -> u8[:] { "circle" }
fn scale(s: Shape.Square, k: i64) -> u8[:] { "square" }

fn main() {
    var ents: Ent[>..] = [Ent { 3, 9 }];
    let e = ents[0];
    var f: f32 = 1.5;
    var g: f64 = 0.25;
    var n64: i64 = 3;
    var tenth: f32 = 0.1;

    // Destinations.
    let a: f64 = 2;
    let b: f32 = e.w;
    print(a, " ", b);                               // 2.0 9.0
    g = n64;
    print(g, " ", wider(e.x), " ", dflt());          // 3.0 3.0 1.0
    let v = V2 { e.x, 2 };
    print(v.x, " ", v.y);                           // 3.0 2.0
    var fs: f32[>..] = [1, e.w];
    fs.push(e.x);
    fs.push(e.w * 0.1);
    print(fs);                                      // [1.0, 9.0, 3.0, 0.90000004]
    f += e.w;
    g -= 1;
    print(f, " ", g);                               // 10.5 2.0
    f = 1.5;

    // Mixed operands: the integer converts to the float's type.
    print(e.w * tenth);                             // 0.90000004
    // An integer inside a block or an inlined call still converts.
    print(({ e.w }) * tenth, " ", next(e.w) * tenth);  // 0.90000004 1.0
    print(n64 + g, " ", n64 * f);                   // 5.0 4.5
    var big: u64 = 18446744073709551615;
    print(big * 1.0, " ", big > 1e19);              // 1.8446744073709552e+19 true
    let packed = Packed { n64 };
    print(packed.n * 0.5, " ", id32(packed.n));     // 1.5 3.0
    print(e.w > f, " ", e.x * 0.5 < f, " ", n64 == 3.0);    // true false true

    // A float of literals and integers takes its destination's type.
    let h = e.w * 0.1;
    print(h);                                       // 0.9
    print(id32(e.w * 0.1), " ", id64(e.w * 0.1));   // 0.90000004 0.9
    let w = V2 { e.w * 0.1, e.w * 0.5 };
    print(w.x, " ", w.y);                           // 0.90000004 4.5
    print(e.w * 0.1 - f);                           // -0.59999996
    print(-(e.w * 0.1) + f);                        // 0.59999996
    print(e.w * 0.1 + 1 + f);                       // 3.4
    print(id32({ e.w * 0.1 }), " ", id32(e.w % 1.5));   // 0.90000004 0.0
    print(g + e.w * 0.1);                           // 2.9

    // An integer literal where a float is expected.
    print(f * 2, " ", g / 4, " ", (g + 1.5) / 2);   // 3.0 0.5 1.75
    let odd: f32 = 16777217;
    print(odd);                                     // 16777216.0

    // A constant beyond f32's range rounds to an infinity at an f32 (only an
    // infinity is its own double here).
    let huge: f32 = 1.0e39;
    let low = id32(-1.0e300);
    let prod = f * 1.0e300;
    print(huge == huge * 2, " ", low == low * 2, " ", low < 0, " ", prod == prod * 2);
                                                    // true true true true
    print(id32(3.4028235e38));                      // 3.4028235e+38

    // Branches joining an integer and a float.
    var c = e.x > 1;
    let j = if c { e.x } else { 0.5 };
    let k = if c { 1 } else { f };
    print(j, " ", k);                               // 3.0 1.0

    // Generic functions: the typed argument binds, a float literal binds
    // before an integer one.
    print(min(f, 1), " ", max(1, 0.5), " ", max(e.x * 0.5, f));    // 1.0 1.0 1.5
    print(clamp(e.w - 0.5, 0.0, f), " ", abs(-e.x));    // 1.5 3

    // Overloads.
    print(pick(2), " ", wide(e.x), " ", id32(e.x), " ", prec(e.x * 0.5));  // i32 i64 3.0 f64
    print(str(0.5), " ", str(e.x));                 // user str 3
    let sh: Shape = if c { Shape.Circle { 1.0 } } else { Shape.Square { 2.0 } };
    print(area(sh, 2), " ", scale(sh, 2));          // 6.0 circle
}
