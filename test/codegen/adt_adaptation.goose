// Variant values constructing their ADT (§3.5) from sources other than a
// literal -- a local, a parameter, a field, an element, a global, a call's
// result (inlined or not, a builtin's, a function value's), a branch, a
// break -- in every position: a declaration, an assignment, an argument, a
// return (forwarded from a multi-value call too), a field, an element; in
// fixed and in variable mode, with fixed, variable-size and resizable
// payloads. An ADT result received in its other mode takes the same paths.
enum Shape { Circle { r: i64 }, Square { s: i64 }, Dot }
enum Msg { Ping, Text { body: u8[] } }
enum Value { A { items: i64[>..] }, B { n: i64 } }
enum Opt<T> { Some { v: T }, None }

fn area(c: Shape.Circle) -> i64 { c.r * 3 }
fn area(s: Shape.Square) -> i64 { s.s * s.s }
fn area(d: Shape.Dot) -> i64 { 0 }
fn describe(p: Msg.Ping) -> i64 { 0 }
fn describe(t: Msg.Text) -> i64 { t.body.len * 1000 + t.body[0] as i64 }
fn tag(v: Value..&) -> i64 { match v { A => -1, B b => b.n } }
fn get(o: Opt<i64>.Some) -> i64 { o.v }
fn get(o: Opt<i64>.None) -> i64 { -1 }

fn circle(r: i64) -> Shape.Circle { Shape.Circle { r: r } }
fn text(s: u8[:]) -> Msg.Text { Msg.Text { body: s } }
fn valb(n: i64) -> Value.B { Value.B { n: n } }
fn vala() -> Value.A { Value.A { items: [1, 2] } }
fn show(s: Shape) -> i64 { area(s) }
fn showv(s: Shape..) -> i64 { area(s) }
fn showm(m: Msg..) -> i64 { describe(m) }
fn widen(c: Shape.Circle) -> Shape { c }
fn widen_ret(s: Shape.Square) -> Shape { return s; }
fn widenv(r: i64) -> Shape.. { circle(r) }
fn widenm(s: u8[:]) -> Msg.. { text(s) }
fn fixed(r: i64) -> Shape { if r > 9 { Shape.Square { s: r } } else { Shape.Circle { r: r } } }
fn varmode(r: i64) -> Shape.. { if r > 9 { Shape.Square { s: r } } else { Shape.Circle { r: r } } }
fn apply<F>(x: i64) -> Shape { F(x) }

var gn: i32 = 40;
fn two(r: i64) -> Shape.Circle, i64 { return Shape.Circle { r: r }, r + 1; }
fn late(r: i64) -> i64, Shape.Square { return r * 2, Shape.Square { s: r }; }
fn nums() -> i32, i32& { return 7, gn; }
fn fwd(r: i64) -> Shape, i64 { return two(r); }
fn fwdv(r: i64) -> Shape.., i64 { return two(r); }
fn fwd_late(r: i64) -> i64, Shape { return late(r); }
fn fwd_nums() -> i64, i64 { return nums(); }

struct HasC { c: Shape.Circle }
struct Holder { s: Shape }
struct VHolder { n: i64, s: Shape.. }

let gc = Shape.Circle { r: 11 };
var gs: Shape = gc;
var gv: Shape.. = gc;

fn main() {
    // Fixed mode.
    let c = Shape.Circle { r: 1 };
    let sq = Shape.Square { s: 2 };
    let d = Shape.Dot {};
    var s: Shape = c;
    print(area(s));                                                    // 3
    s = sq;
    print(area(s), " ", show(c), " ", show(widen(c)), " ", show(widen_ret(sq)));  // 4 3 3 4
    s = circle(5);
    print(area(s), " ", show(circle(6)));                              // 15 18
    let h = HasC { c: circle(7) };
    var hold = Holder { s: h.c };
    let before = area(hold.s);
    hold.s = sq;
    print(before, " ", area(hold.s));                                  // 21 4
    let cs: Shape.Circle[2] = [c, circle(8)];
    let arr: Shape[3] = [cs[1], d, circle(9)];
    print(area(arr[0]), " ", area(arr[1]), " ", area(arr[2]));         // 24 0 27
    var g: Shape[>..] = [c];
    g.push(circle(10));
    g.push(sq);
    print(area(g[0]), " ", area(g[1]), " ", area(g[2]));               // 3 30 4
    var ring: Shape.Circle[>..] = [];
    ring.push(circle(12));
    let top: Shape = ring.push(circle(13));
    ring.pop();
    let popped: Shape = ring.pop();
    print(area(top), " ", area(popped));                               // 39 36
    s = if area(c) > 5 { c } else { sq };
    let m: Shape = match g[1] { Circle x => x, _ => sq };
    print(area(s), " ", area(m));                                      // 4 30
    var n = 0;
    let lb: Shape = loop { n++; if n > 2 { break circle(n); } };
    let bl: Shape = { let t = circle(14); t };
    let fv: Shape = apply(15) { circle(it) };
    print(area(lb), " ", area(bl), " ", area(fv));                     // 9 42 45
    let some = Opt<i64>.Some { v: 5 };
    var o: Opt<i64> = some;
    let got = get(o);
    o = Opt<i64>.None {};
    print(got, " ", get(o));                                           // 5 -1
    let g0 = area(gs);
    gs = circle(16);
    print(g0, " ", area(gs), " ", area(gv));                           // 33 48 33

    // Variable mode.
    let v: Shape.. = c;
    let v2: Shape.. = circle(17);
    print(area(v), " ", area(v2));                                     // 3 51
    print(showv(sq), " ", showv(circle(18)), " ", area(widenv(19)));   // 4 54 57
    let vh = VHolder { n: 1, s: circle(20) };
    ring.push(circle(22));
    let vp: Shape.. = ring.pop();
    print(area(vh.s), " ", area(vp));                                  // 60 66
    var vs: Shape..[>..] = [];
    vs.push(c);
    vs.push(circle(21));
    vs.push(sq);
    for x in vs { print(area(x)); }                                    // 3, 63, 4
    let vm: Shape.. = match v2 { Circle x => x, _ => sq };
    print(area(vm));                                                   // 51
    let mf: Shape.. = fixed(12);
    let fm: Shape = varmode(4);
    print(area(mf), " ", area(fm));                                    // 144 12

    // Variable-size payloads.
    let t = Msg.Text { body: "hey" };
    let m1: Msg.. = copy(t);
    let m2: Msg.. = text("ab");
    print(describe(m1), " ", describe(m2));                            // 3104 2097
    print(showm(copy(t)), " ", showm(text("xyz")), " ", showm(widenm("q")));  // 3104 3120 1113
    var ms: Msg..[>..] = [];
    ms.push(text("abcd"));
    ms.push(Msg.Ping);
    for x in ms { print(describe(x)); }                                // 4097, 0

    // A resizable ADT: the variant's own tail, or none.
    let b = Value.B { n: 9 };
    var r1: Value.. = b;
    var r2: Value.. = valb(10);
    var r3: Value.. = vala();
    print(tag(r1), " ", tag(r2), " ", tag(r3));                        // 9 10 -1

    // Forwarded multi-value returns.
    let a1, b1 = fwd(3);
    print(area(a1), " ", b1);                                          // 9 4
    let a2, b2 = fwdv(4);
    print(area(a2), " ", b2);                                          // 12 5
    let a3, b3 = fwd_late(5);
    print(a3, " ", area(b3));                                          // 10 25
    let a4, b4 = fwd_nums();
    print(a4, " ", b4);                                                // 7 40
}
