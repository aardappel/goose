// Equality of fixed values made only of integers and bools (§4.5), on both
// sides of the size where the C compares the bytes as one run rather than
// field by field: every field differing in turn, nested structs and arrays,
// pads (never compared), operands freshly built from loads, array elements
// in place, and slices of such values.

struct Pos { y: i64, x: i64 }
struct Mix { a: u8, b: u16, c: i32, d: i64 }
struct Five { a: i32, b: i32, c: i32, d: i32, e: i32 }
struct Pair { p: Pos, q: Pos }
struct Flags { on: bool, n: i16 }
struct Quad { v: i16[4] }
struct Wide { v: u8[5] }
struct Padded { a: u8, pad 3, b: i32 }
struct Linked { kind: u8, link: (Linked&<u32>)? }

fn same<T>(x: T[:], y: T[:]) -> bool { x == y }

fn main() {
    // Read from an array, so no value is known to the optimizer.
    var src: i64[>..] = [1, 2, 3, 4, 5];
    let a = src[0];
    let b = src[1];
    let c = src[2];
    let d = src[3];
    let e = src[4];

    let p = Pos { y: a, x: b };
    print(p == Pos { y: a, x: b }, " ", p == Pos { y: b, x: b }, " ",
          p == Pos { y: a, x: a }, " ", p != Pos { y: a, x: b });              // true false false false

    let m = Mix { a: a as! u8, b: b as! u16, c: c as! i32, d: d };
    print(m == Mix { a: a as! u8, b: b as! u16, c: c as! i32, d: d }, " ",
          m == Mix { a: b as! u8, b: b as! u16, c: c as! i32, d: d }, " ",
          m == Mix { a: a as! u8, b: a as! u16, c: c as! i32, d: d }, " ",
          m == Mix { a: a as! u8, b: b as! u16, c: d as! i32, d: d }, " ",
          m == Mix { a: a as! u8, b: b as! u16, c: c as! i32, d: e });        // true false false false false

    let f = Five { a: a as! i32, b: b as! i32, c: c as! i32, d: d as! i32, e: e as! i32 };
    var fives: Five[>..] = [];
    for i in 6 {
        var g = f;
        if i == 1 { g.a = 0; }
        if i == 2 { g.b = 0; }
        if i == 3 { g.c = 0; }
        if i == 4 { g.d = 0; }
        if i == 5 { g.e = 0; }
        fives.push(g);
    }
    for g in fives { print(f == g, " ", g == f); }                            // true true, then false false x5

    let pq = Pair { p: Pos { y: a, x: b }, q: Pos { y: c, x: d } };
    print(pq == Pair { p: Pos { y: a, x: b }, q: Pos { y: c, x: d } }, " ",
          pq == Pair { p: Pos { y: a, x: b }, q: Pos { y: c, x: e } }, " ",
          pq == Pair { p: Pos { y: e, x: b }, q: Pos { y: c, x: d } });       // true false false

    let fl = Flags { on: a == 1, n: b as! i16 };
    print(fl == Flags { on: true, n: 2 }, " ", fl == Flags { on: false, n: 2 }, " ",
          fl == Flags { on: true, n: 3 });                                    // true false false

    let qd = Quad { v: [a as! i16, b as! i16, c as! i16, d as! i16] };
    print(qd == Quad { v: [1, 2, 3, 4] }, " ", qd == Quad { v: [1, 2, 3, 5] }, " ",
          qd.v == Quad { v: [1, 2, 3, 4] }.v, " ", qd.v == Quad { v: [0, 2, 3, 4] }.v); // true false true false

    let w = Wide { v: [a as! u8, b as! u8, c as! u8, d as! u8, e as! u8] };
    print(w == Wide { v: [1, 2, 3, 4, 5] }, " ", w == Wide { v: [1, 2, 3, 4, 6] }, " ",
          w == Wide { v: [0, 2, 3, 4, 5] });                                  // true false false

    let pd = Padded { a: a as! u8, b: b as! i32 };
    print(pd == Padded { a: 1, b: 2 }, " ", pd == Padded { a: 1, b: 3 });     // true false

    // Elements compared where they are, and slices of them.
    var ps: Pos[>..] = [];
    var qs: Pos[>..] = [];
    for i in 4 {
        ps.push(Pos { y: src[i], x: src[i + 1] });
        qs.push(Pos { y: src[i], x: src[i + 1] });
    }
    print(ps[1] == qs[1], " ", ps[1] == qs[2], " ", same(ps[..], qs[..]));    // true false true
    qs[3].x = 0;
    print(same(ps[..], qs[..]), " ", same(ps[0..3], qs[0..3]), " ", ps == qs); // false true false
    var hits = 0;
    for q in ps {
        if q == (Pos { y: c, x: d }) { hits += 10; }
        if q == ps[0] { hits++; }
    }
    print(hits);                                                              // 11

    var ls: Linked[>..] = [];
    for i in 3 { ls.push(Linked { kind: src[i % 2] as! u8, link: null }); }
    print(ls[0] == ls[2], " ", ls[0] == ls[1]);                               // true false
}
