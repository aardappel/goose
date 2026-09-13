// Two fat-reference parameters with distinct root classes can still name one
// stack, reached through a caller's parameter or through a captured local.
// Two locals a recursion or a wrapper passes on stay two stacks, but a class
// passed on beside a global may be that global, two classes of a wrapper
// whose argument was only inexactly rooted may be one stack, and a class a
// function value captured may be the class of the function running it.
struct Bag { tag: i64, items: i64[>..] }
struct Outer { id: i64, bag: Bag }
var global: Outer = Outer { id: 1, bag: Bag { tag: 2, items: [] } };
recursive fn fill2(xs: i64[>..]&, b: Bag&, n: i64) {
    if n > 0 { fill2(xs, b, n - 1); return; }
    xs.push(10);
    b.items.push(20);
    xs.push(30);
}
recursive fn caller(b: Bag&, n: i64) {
    if n > 0 { caller(b, n - 1); return; }
    fill2(global.bag.items, b, 1);
}
var g1: i64[>..] = [];
var g2: i64[>..] = [];
var g3: i64[>..] = [];
recursive fn twice(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    fn again(m: i64) { twice(xs, g1, m); }
    if n > 0 { again(n - 1); return; }
    xs.push(1);
    ys.push(2);
    xs.push(3);
}
recursive fn build(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { build(xs, ys, n - 1); }
    xs.push(n);
    ys.push(n * 2);
    xs.push(n + 100);
}
fn wrap(xs: i64[>..]&, ys: i64[>..]&) { build(xs, ys, 1); }
recursive fn fill3(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { fill3(xs, ys, n - 1); return; }
    xs.push(1);
    ys.push(2);
    xs.push(3);
}
fn pass3(xs: i64[>..]&, ys: i64[>..]&) { fill3(xs, ys, 1); }
recursive fn fill4(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { fill4(xs, ys, n - 1); return; }
    xs.push(1);
    ys.push(2);
    xs.push(3);
}
fn pass4(xs: i64[>..]&, ys: i64[>..]&) { fill4(xs, ys, 1); }
recursive fn grow(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { grow(xs, ys, n - 1); return; }
    for i in 3 { xs.push(i); ys.push(i * 10); }
}
fn times<F>(n: i64) { for i in n { F(i); } }
fn grow_each(xs: i64[>..]&, ys: i64[>..]&) { times(2) { i => grow(xs, ys, i) } }
recursive fn fill5(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { fill5(xs, ys, n - 1); return; }
    xs.push(1);
    ys.push(2);
    xs.push(3);
}
fn apply1<F>(zs: i64[>..]&) { F(zs); }
fn beside(xs: i64[>..]&) { apply1(xs) { u => fill5(xs, u, 1) } }
fn main() {
    caller(global.bag, 1);
    print(global.bag.items);
    var o: Outer = Outer { id: 1, bag: Bag { tag: 2, items: [] } };
    fn inner(b: Bag&) { fill2(o.bag.items, b, 1); }
    inner(o.bag);
    print(o.bag.items);
    var a: i64[>..] = [];
    var c: i64[>..] = [];
    build(a, c, 2);
    print(a, " ", c);
    twice(g3, g1, 1);
    twice(g1, g2, 1);
    print(g1, " ", g2, " ", g3);
    var p: i64[>..] = [];
    var q: i64[>..] = [];
    wrap(p, q);
    print(p, " ", q);
    var d: i64[>..] = [];
    var e: i64[>..] = [];
    let pick = d.len > 0;
    let de: i64[>..]& = if pick { d } else { e };
    pass3(de, e);
    print(d, " ", e);
    var u: i64[>..] = [];
    var w: i64[>..] = [];
    pass4(u, w);
    let longer = u.len > 5;
    let uw: i64[>..]& = if longer { u } else { w };
    pass4(uw, w);
    print(u, " ", w);
    var s: i64[>..] = [];
    var t: i64[>..] = [];
    grow_each(s, t);
    print(s, " ", t);
    var x: i64[>..] = [];
    beside(x);
    print(x);
}
