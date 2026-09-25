// runtime-debug: a function that builds a value on a data stack takes its
// stack indices above its callers' (§6.2) also where the checker fitted that
// value to a fixed-size slot: a str() passed as a slice or returned as a
// u8[..16], a to_bytes() passed as a slice, a struct formatted into a limited
// array, a payload a match arm copies out of a reference, a scrutinee copied
// for a dispatch arm, the variable-mode pointee of a reference result adapted
// to a fixed-mode ADT. Each driver below keeps a builder growing on its first
// stack meanwhile, the one such a function's first temporary would land on
// if it counted its stacks from 0: the chains append every level's text to
// the builder they pass down while the level's str() is live, and the other
// drivers keep their builder's top in a local across calls that cannot
// reach it.

// The pattern a code emitter builds its text with: each level appends its
// text to one builder and hands back the part it appended.
fn own(text: u8[>..]&, s: const u8[:]) -> const u8[:] {
    let begin = text.len;
    text.append(s);
    return text[begin..];
}
fn w0(text: u8[>..]&) -> const u8[:] { return own(text, "x"); }
fn w1(text: u8[>..]&) -> const u8[:] { let child = w0(text); return own(text, str("(", child, ")")); }
fn w2(text: u8[>..]&) -> const u8[:] { let child = w1(text); return own(text, str("(", child, ")")); }
fn w3(text: u8[>..]&) -> const u8[:] { let child = w2(text); return own(text, str("(", child, ")")); }

fn chain() {
    var b: u8[>..] = [];
    print(w3(b), " ", b);                // (((x))) x(x)((x))(((x)))
}

recursive fn nest(text: u8[>..]&, n: i64) -> const u8[:] {
    if n == 0 { return own(text, "x"); }
    let child = nest(text, n - 1);
    return own(text, str("<", child, ":", n, ">"));
}

fn recursion() {
    var r: u8[>..] = [];
    let top = nest(r, 4);
    print(top.len, " ", r.len);          // 17 45
    print(top);                          // <<<<x:1>:2>:3>:4>
    print(r);                            // x<x:1><<x:1>:2><<<x:1>:2>:3><<<<x:1>:2>:3>:4>
}

// A str() built as the u8[..16] the function returns.
fn label(n: i64) -> u8[..16] { return str("n", n); }

fn labels() {
    var b: u8[>..] = [];
    var l: u8[..16] = [];
    for i in 3 {
        b.push(65 + i as u8);
        l = label(i * 7);
    }
    print(b, " ", l);                    // ABC n14
}

// A to_bytes() image passed as a slice.
fn width(s: const u8[:]) -> i64 { return s.len; }
fn image_width(a: const i32[:]) -> i64 { return width(to_bytes(a)); }

fn images() {
    var b: u8[>..] = [];
    var w = 0;
    let arr: i32[3] = [1, 2, 3];
    for i in 3 {
        b.push(97 + i as u8);
        w += image_width(arr);
    }
    print(b, " ", w);                    // abc 39
}

// A struct rendered into a limited array.
struct P { x: i32, y: i32 }
fn put(l: u8[..48]&, p: P) { format(l, p); }

fn formats() {
    var b: u8[>..] = [];
    var out: u8[..48] = [];
    for i in 3 {
        b.push(48 + i as u8);
        put(out, P { x: i as i32, y: 1 });
    }
    print(b, " ", out);                  // 012 P { 0, 1 }P { 1, 1 }P { 2, 1 }
}

// A payload copied out of a reference by a match arm that binds it by value,
// and the whole value copied for a dispatch arm that takes its variant by
// value.
enum Msg { Ping, Text { body: u8[] } }
fn texts(m: Msg..&) -> i64 { return match m { Ping => 0, Text t => 1 }; }
fn weight(p: Msg.Ping) -> i64 { 10 }
fn weight(t: Msg.Text&) -> i64 { 20 }
fn total(m: Msg..&) -> i64 { return weight(m); }

fn matches() {
    var b: u8[>..] = [];
    var m: Msg.. = Msg.Text { body: "some text" };
    var n = 0;
    for i in 3 {
        b.push(120 + i as u8);
        n += texts(m);
    }
    print(b, " ", n);                    // xyz 3
}

fn dispatches() {
    var b: u8[>..] = [];
    var m: Msg.. = Msg.Text { body: "some text" };
    var n = 0;
    for i in 3 {
        b.push(117 + i as u8);
        n += total(m);
    }
    print(b, " ", n);                    // uvw 60
}

// The variable-mode pointee of a reference result, adapted to the
// fixed-mode ADT.
enum Shape { Circle { r: i64 }, Square { s: i64 } }
fn pick(a: Shape..&) -> Shape..& { return a; }
fn area(v: Shape..&) -> i64 {
    let s: Shape = pick(v);
    return match s { Circle c => c.r, Square q => q.s * q.s };
}

fn shapes() {
    var b: u8[>..] = [];
    var sh: Shape.. = Shape.Square { s: 5 };
    var n = 0;
    for i in 3 {
        b.push(107 + i as u8);
        n += area(sh);
    }
    print(b, " ", n);                    // klm 75
}

fn main() {
    chain();
    recursion();
    labels();
    images();
    formats();
    matches();
    dispatches();
    shapes();
}
