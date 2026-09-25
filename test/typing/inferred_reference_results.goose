// warning: redundant &: c.other is returned by reference without it
// A function whose result type is inferred takes it from its returns as an
// un-annotated `let` takes its initializer's (§4.1, §7.1): a reference to a
// non-fixed value is returned as that reference, and a non-fixed lvalue other
// than the function's own local by reference, from a `return` or the body's
// tail alike -- a reference field reached through a parameter, a frame
// object's tail, a variable-size struct behind a reference parameter, an
// untyped parameter, a global, a local of the enclosing function, each value
// of a multi-value return, a call returning such a reference. An explicit
// `&x` makes the result a reference whatever x's size class. Pushes and
// writes through the result reach the original, and so do those through a
// variable it initializes. The function's own local still moves, a reference
// to a fixed-size value still decays to a copy, and copy() copies.
struct Ctx { args: i64[>..<]&, other: i64[>..<]&, n: i64& }
struct Named { name: u8[], n: i64 }            // variable-size
struct Frame { depth: i64, marks: i64[>..] }   // a frame object
struct P { x: i64, y: i64 }

var log: i64[>..] = [];

fn args(c: Ctx&) { return c.args; }
fn argstail(c: Ctx&) { c.args }
fn both(c: Ctx&) { return c.args, c.other; }
fn either(c: Ctx&, k: bool) {
    if !k { return &c.other; }
    return c.args;
}
fn through(c: Ctx&) { args(c) }
fn same(x) { x }
fn marks(f: Frame&) { f.marks }
fn whole(p: Named&) { p }
fn logged() { log }
fn xref(p: P&) { return &p.x; }
fn count(c: Ctx&) { c.n }
fn fresh() {
    var t: i64[>..<] = [7];
    t.push(8);
    t
}
fn nested() -> i64 {
    var t: i64[>..] = [1];
    fn mine() { t }
    mine().push(2);
    let m = mine();
    m.push(3);
    return t.len;
}
// A function outside a cycle returns its inferred reference into one.
recursive fn fill(c: Ctx&, k: i64) {
    args(c).push(k);
    if k > 0 { fill(c, k - 1); }
}

fn main() {
    var s: i64[>..<] = [];
    var t: i64[>..<] = [];
    var n = 1;
    var c = Ctx { args: s, other: t, n: n };
    args(c).push(1);
    let a = argstail(c);
    a.push(2);
    let x, y = both(c);
    x.push(3);
    y.push(4);
    either(c, false).push(5);
    either(c, true).push(6);
    through(c).push(7);
    var sm = same(s);
    sm.push(8);
    print(s, " ", t, " ", args(c).len, " ", args(c)[0]);   // [1, 2, 3, 6, 7, 8] [4, 5] 6 1

    var fr = Frame { depth: 3, marks: [] };
    marks(fr).push(fr.depth);
    let mk = marks(fr);
    mk.push(4);
    var nm = Named { name: "name", n: 1 };
    let w = whole(nm);
    w.n = 2;
    w.name[0] = 'N';
    logged().push(1);
    let lg = logged();
    lg.push(2);
    print(fr.marks, " ", nm.name, " ", nm.n, " ", log, " ", nested());   // [3, 4] Name 2 [1, 2] 3

    var p = P { x: 1, y: 2 };
    var r .= xref(p);
    r = 9;
    let v = xref(p);        // a fixed-size pointee: a copy
    r = 10;
    var k = count(c);       // decays to the i64
    k = 100;
    var f = fresh();        // moved out of the function
    f.push(9);
    var cp = copy(args(c));
    cp.push(100);
    print(p.x, " ", v, " ", n, " ", f, " ", cp.len, " ", s.len);   // 10 9 1 [7, 8, 9] 7 6

    s.clear();
    fill(c, 2);
    let deep = through(c);
    deep.push(-1);
    print(s, " ", same(n) + 1);   // [2, 1, 0, -1] 2
}
