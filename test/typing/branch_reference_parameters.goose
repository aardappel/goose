// warning: redundant &: a binds by reference here without it
// warning: redundant &: b binds by reference here without it
// A reference parameter binds each branch of an if, match, block, loop or
// bare { } by reference, whatever the branch's size class (§4.1), as an
// annotated reference `let` binds it: a fixed-size variable, field or
// element, a slice's slot, or what a reference names. The callee writes the
// storage the branch taken names, and a reference it returns is rooted
// where the branches are.
struct P { x: i64, y: i64 }
enum Shape { Circle { r: i64 }, Square { s: i64 } }
struct V { n: varint, m: varint }

var g: u8[>..] = [];
var h: u8[>..] = [];
var ga: i64[3] = [1, 2, 3];
var gi = 1;
var gj = 2;

fn set(v: i64&, n: i64) { v = n; }
fn shift(p: P&) { p.x += 100; }
fn repoint(p: u8[:]&) { p = g[1..]; }
fn id(x: i64[3]&) -> i64[3]& { x }
fn same(v: i64&) -> i64& { v }
fn look(v: const i64&) -> i64 { v }
fn put<T>(dst: T&, v: T) { dst = v; }
fn tag(c: Shape.Circle, n: i64&) { n = c.r; }
fn tag(s: Shape.Square, n: i64&) { n = s.s; }
fn each<F>(c: bool, x: i64&, y: i64&) { F(if c { x } else { y }); }
fn fallback(n: i64, v: i64& = if gi > 1 { gi } else { gj }) { v = n; }
fn wide(r: const varint&) -> i64 { r }

fn main() {
    var a = 1;
    var b = 2;
    let c = a < b;
    let d = a > b;

    // Each construct, with fixed-size branches.
    set(if c { a } else { b }, 10);
    set(if d { a } else { b }, 20);
    print(a, " ", b);
    set(match a { 10 => b, _ => a }, 30);
    set(block { if d { break a; } b }, 40);
    set(loop { break a; }, 50);
    set({ b }, 60);
    print(a, " ", b);

    // Struct, field, element and array branches.
    var p1 = P { x: 1, y: 2 };
    var p2 = P { x: 3, y: 4 };
    shift(if c { p1 } else { p2 });
    set(if d { p1.x } else { p2.y }, 5);
    var arr: i64[>..] = [7, 8, 9];
    set(if c { arr[2] } else { arr[0] }, 90);
    id(block { ga })[1] = 20;
    print(p1.x, " ", p2.y, " ", arr[2], " ", ga[1]);

    // Slices bind the slot they lie in: the callee re-points the variable.
    g.append("xyz");
    h.append("abcd");
    var s1 = h[..];
    var s2 = h[1..];
    repoint(if d { s1 } else { s2 });
    print(s1.len, " ", s2.len);
    repoint(match s1.len { 4 => s1, _ => s2 });
    print(s1, " ", s2);

    // References name what they bind, a call's included; `&x` is redundant.
    let r: i64& = a;
    set(if c { r } else { b }, 70);
    set(if d { same(a) } else { same(b) }, 80);
    set(if c { &a } else { &b }, 71);
    print(a, " ", b);

    // Generic, UFCS, dispatch, a function value's parameter, a default,
    // and a returned reference.
    put(if d { p1 } else { p2 }, P { x: 11, y: 12 });
    (if c { a } else { b }).set(72);
    let sh: Shape = Shape.Square { s: 33 };
    tag(sh, if d { a } else { b });
    print(p2.x, " ", a, " ", b, " ", look(if c { a } else { b }));
    each(c, a, b) { v: i64& => v = 42; };
    fallback(15);
    let q .= same(if d { a } else { b });
    q = 99;
    print(a, " ", b, " ", gi, " ", gj);

    // A varint field binds as the varint it is (§3.6).
    let v = V { n: 300, m: 5 };
    print(wide(if c { v.n } else { v.m }), " ", wide(block { if d { break v.n; } v.m }));
}
