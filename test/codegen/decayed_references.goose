// An explicit `&x` where the reference reads as its pointee (§3.8): as the
// operand of an operator or a cast, as a value printed or formatted, at a
// value-typed destination (a binding, a by-value argument, a return, a
// literal's field or element, an assignment's right-hand side, copy()), as
// a condition and as an index. Each reads x's value, for a scalar, a
// struct, a slice variable and field, a reference parameter, a narrowed
// optional and a varint field.
struct P { x: i32, y: i32 }
struct Ints { xs: const i32[:] }
struct V { n: varint, w: i32 }

fn twice(a: i32) -> i32 { return a * 2; }
fn back() -> i32 {
    var x = 21 as i32;
    return &x;
}
fn both() -> i32, i32 {
    var x = 1 as i32;
    var y = 2 as i32;
    return &x, &y;
}
fn through(r: i32&, q: const u8[:]&) {
    print(&r + 1, " ", &r == 5, " ", &q, " ", str("<", &q, ">"));
}

fn main() {
    var a = 3;
    var b = 3;
    var p = P { 1, 2 };
    var q = P { 1, 2 };
    let arr: i32[] = [1, 2];
    var n = Ints { arr[..] };
    var m = Ints { arr[..] };
    var s: const u8[:] = "ab";
    var no = false;

    // Operands.
    print(&a == &b, " ", &p == &q, " ", &n.xs == &m.xs, " ", &s != "ab");
                                                        // true true true false
    print(&p.x + &q.y, " ", -&a, " ", &a * 2, " ", &a < 4, " ", !&no);
                                                        // 3 -3 6 true true
    print((&p.y) as i64, " ", ((&a) as f64) / 2.0);    // 2 1.5

    // Rendered values: print, str and format.
    print(&a, " ", &p, " ", &n.xs, " ", &s);            // 3 P { 1, 2 } [1, 2] ab
    print(str("[", &s, "]", &a));                       // [ab]3
    var out: u8[>..] = [];
    format(out, &s, &n.xs, &a);
    print(out);                                         // ab[1, 2]3

    // Value destinations.
    let y: i32 = &p.y;
    let t: const u8[:] = &s;
    let lit = P { &q.y, &p.x };
    let pair: i64[2] = [&a, &b];
    print(y, " ", t, " ", lit, " ", pair);              // 2 ab P { 2, 1 } [3, 3]
    let u, w = both();
    print(twice(&p.y), " ", back(), " ", u, " ", w);    // 4 21 1 2
    var c = 1;
    c += &a;
    var cells: i64[3] = [0, 0, 0];
    cells[1] = &c;
    let cp = copy(&p);
    print(c, " ", cells, " ", cp);                      // 4 [0, 4, 0] P { 1, 2 }

    // Conditions and indices.
    if &no { print("taken"); } else { print("not taken"); }   // not taken
    var k = 0;
    while &k < 2 { k++; }
    var i = 1;
    let xs: i32[3] = [7, 8, 9];
    cells[&i] = 5;
    print(k, " ", xs[&i], " ", cells);                  // 2 8 [0, 5, 0]

    // A reference parameter, a narrowed optional and a varint field.
    var five = 5 as i32;
    through(five, s);                                   // 6 true ab <ab>
    var o: i32? = p.y;
    if o { print(&o + 1); }                             // 3
    let v = V { 300, 4 };
    print(&v.n + 1, " ", &v.w);                         // 301 4
}
