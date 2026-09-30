// runtime-debug: print, str and format render a fixed-mode ADT's payload
// from a copy taken once its tag is read (§3.7): an overload rendering one
// part may overwrite the value with another variant (§3.5), and the rest of
// the payload is still the variant the rendering began with, wherever the
// value lies. What follows the value is read where it lies, as it is then.

// A slice after the part, which the new variant's bytes would make wild.
struct A { v: i64 }
enum Shape {
    Circle { a: A, s: i64[:] },
    Rect { w: i64, h: i64 },
    Dot,
}
var data: i64[>..] = [];
var g: Shape = Shape.Dot;
fn format(out: u8[>..]&, a: A) {
    g = Shape.Rect { w: 4096, h: 1000000 };
    format(out, "A", a.v);
}

// An overload taking the part by reference is given the copy's.
struct B { v: i64 }
enum Pair {
    Two { b: B, n: i64 },
    Wide { x: i64, y: i64 },
}
var gp: Pair = Pair.Wide { x: 0, y: 0 };
fn format(out: u8[>..]&, b: B&) {
    gp = Pair.Wide { x: 4096, y: 77 };
    format(out, "B", b.v);
}

// In a struct, among elements, behind references, in another payload.
struct C { v: i64 }
enum Sh {
    Round { c: C, s: i64[:] },
    Flat { w: i64, h: i64 },
}
struct Box { k: i64, sh: Sh, n: i64 }
enum Outer {
    Wrap { sh: Sh, m: i64 },
    Empty,
}
var box = Box { k: 1, sh: Sh.Flat { w: 0, h: 0 }, n: 2 };
var shapes: Sh[>..] = [];
var fixed: Sh[2] = [Sh.Flat { w: 0, h: 0 }, Sh.Flat { w: 0, h: 0 }];
var outer: Outer = Outer.Empty;
fn format(out: u8[>..]&, c: C) {
    box.sh = Sh.Flat { w: 4096, h: 1000000 };
    box.n = 3;
    for i in 0..shapes.len { shapes[i] = Sh.Flat { w: 4096, h: 1000000 }; }
    fixed[0] = Sh.Flat { w: 4096, h: 1000000 };
    fixed[1] = Sh.Flat { w: 4096, h: 1000000 };
    outer = Outer.Empty;
    format(out, "C", c.v);
}
fn box_shape() -> Sh& { return box.sh; }

// In a variable-mode payload, replaced through a reference to its field.
struct D { v: i64 }
enum Dh {
    Pin { d: D, s: i64[:] },
    Bar { w: i64, h: i64 },
}
enum Named {
    Called { dh: Dh, name: u8[] },
    Anon { x: i64 },
}
var named: Named.. = Named.Called { dh: Dh.Bar { w: 0, h: 0 }, name: "nm" };
var dref: Dh? = null;
fn format(out: u8[>..]&, d: D) {
    if dref { dref = Dh.Bar { w: 4096, h: 1000000 }; }
    format(out, "D", d.v);
}
fn aim() {
    match named {
        Called &c => { dref .= c.dh; },
        Anon => {},
    }
}

// An overload for the variant type renders the variant's own values, while
// an ADT holding that variant renders as its literal.
enum Tone {
    Hi { f: i64 },
    Lo { f: i64 },
}
struct Mix { t: Tone, h: Tone.Hi }
fn format(out: u8[>..]&, h: Tone.Hi) { format(out, "hi", h.f); }

// A payload with `in pool` references, which a copy keeps (§3.9), is
// rendered from the copy like any other.
struct T { x: i64 }
struct L { v: i64 }
var tpool: T[>..] = [];
enum Link {
    To { t: T&<u16 in tpool>, k: i64 },
    Nowhere,
}
struct Blob { l: L, link: Link }
var blobs: Blob[>..] = [];
fn format(out: u8[>..]&, l: L) { format(out, "L", l.v); }

fn main() {
    data.push(1);
    data.push(2);
    g = Shape.Circle { a: A { v: 5 }, s: data[..] };
    print(g);                                   // Circle { A5, [1, 2] }
    print(g);                                   // Rect { 4096, 1000000 }
    g = Shape.Circle { a: A { v: 5 }, s: data[..] };
    print(g, " ", g);                           // Circle { A5, [1, 2] } Rect { 4096, 1000000 }
    g = Shape.Circle { a: A { v: 6 }, s: data[..] };
    print(str(g));                              // Circle { A6, [1, 2] }
    g = Shape.Circle { a: A { v: 7 }, s: data[..] };
    var buf: u8[>..] = [];
    format(buf, g, "; ", g);
    print(buf);                                 // Circle { A7, [1, 2] }; Rect { 4096, 1000000 }
    g = Shape.Circle { a: A { v: 8 }, s: data[..] };
    var lim: u8[..64] = [];
    format(lim, g);
    print(lim);                                 // Circle { A8, [1, 2] }
    g = Shape.Dot;
    print(g);                                   // Shape.Dot

    gp = Pair.Two { b: B { v: 5 }, n: 6 };
    print(gp);                                  // Two { B5, 6 }
    print(gp);                                  // Wide { 4096, 77 }

    box.sh = Sh.Round { c: C { v: 1 }, s: data[..] };
    print(box);                                 // Box { 1, Round { C1, [1, 2] }, 3 }
    print(box);                                 // Box { 1, Flat { 4096, 1000000 }, 3 }
    shapes.push(Sh.Round { c: C { v: 1 }, s: data[..] });
    shapes.push(Sh.Round { c: C { v: 2 }, s: data[..] });
    print(shapes);                              // [Round { C1, [1, 2] }, Flat { 4096, 1000000 }]
    shapes[0] = Sh.Round { c: C { v: 3 }, s: data[..] };
    print(shapes[..1]);                         // [Round { C3, [1, 2] }]
    fixed[0] = Sh.Round { c: C { v: 4 }, s: data[..] };
    fixed[1] = Sh.Round { c: C { v: 5 }, s: data[..] };
    print(fixed);                               // [Round { C4, [1, 2] }, Flat { 4096, 1000000 }]
    box.sh = Sh.Round { c: C { v: 6 }, s: data[..] };
    let r: Sh& = box.sh;
    print(r);                                   // Round { C6, [1, 2] }
    box.sh = Sh.Round { c: C { v: 7 }, s: data[..] };
    var o: Sh? = null;
    o .= box.sh;
    print(o);                                   // Round { C7, [1, 2] }
    box.sh = Sh.Round { c: C { v: 9 }, s: data[..] };
    print(box_shape());                         // Round { C9, [1, 2] }
    outer = Outer.Wrap { sh: Sh.Round { c: C { v: 8 }, s: data[..] }, m: 9 };
    print(outer);                               // Wrap { Round { C8, [1, 2] }, 9 }
    print(outer);                               // Outer.Empty

    aim();
    if dref { dref = Dh.Pin { d: D { v: 1 }, s: data[..] }; }
    print(named);                               // Called { Pin { D1, [1, 2] }, "nm" }
    print(named);                               // Called { Bar { 4096, 1000000 }, "nm" }

    let m = Mix { t: Tone.Hi { f: 1 }, h: Tone.Hi { f: 2 } };
    print(m);                                   // Mix { Hi { 1 }, hi2 }

    tpool.push(T { x: 1 });
    tpool.push(T { x: 2 });
    blobs.push(Blob { l: L { v: 1 }, link: Link.Nowhere });
    blobs[0].link = Link.To { t: tpool[1], k: 4 };
    print(blobs[0]);                            // Blob { L1, To { T { 2 }, 4 } }
}
