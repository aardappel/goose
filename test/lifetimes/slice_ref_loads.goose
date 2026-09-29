// A slice loaded through a reference to a slice is the one in the slot the
// reference names, as writable as that slice and the reference are (§9.5):
// a slot that is not const takes it, and a write through it lands in the
// array it views. So through a parameter (a recursive fn's included), a
// local reference, a call's reference result or one a field holds, into a
// variable, a field, an element, a global or a result.
struct H { r: u8[:]& }
struct Box { s: u8[:] }
var g: u8[>..] = [];
var gs: u8[:] = g[..];

fn f(p: u8[:]&, q: u8[:]&) { q = p; }

fn pick(p: u8[:]&, q: u8[:]&, first: bool) -> u8[:]& {
    if first { return p; }
    return q;
}

fn load(p: u8[:]&) -> u8[:] { return p; }

// A recursive fn's parameter has no view of its slot's slice: the load is
// as writable as the reference.
recursive fn deep(p: u8[:]&, q: u8[:]&, n: i64) {
    if n > 0 { deep(p, q, n - 1); } else { q = p; }
}

fn up(p: u8[:]&, i: i64) {
    var w = p;
    w[i] -= 32;
}

// A parameter's load stored through another parameter, and a local
// reference's load assigned.
fn reported() {
    g.append("ab");
    var x = g[..];
    var y = g[0..1];
    f(x, y);
    let r = &x;
    var z = g[1..];
    z = r;
    print(y, z);
}

// Into a struct literal, a field, an element, an annotated variable and a
// global; out of a call's reference result, a result, a reference a field
// holds and a recursive callee's parameter.
fn slots() {
    var x = g[2..4];
    var y = g[4..6];
    let r = &x;
    var b = Box { s: r };
    var arr: u8[:][>..] = [];
    arr.push(r);
    var a: u8[:] = pick(x, y, false);
    let h = H { r: y };
    var c: u8[:] = h.r;
    var d: u8[:] = load(x);
    print(b.s, " ", arr[0], " ", a, " ", c, " ", d);
    b.s = pick(x, y, false);
    a = pick(x, y, true);
    gs = r;
    var m = g[..0];
    deep(y, m, 2);
    print(b.s, " ", a, " ", gs, " ", m);
}

// The loaded slice writes the array it views, whether the slot is a
// variable or an element.
fn writes() {
    var x = g[2..4];
    var arr: u8[:][>..] = [];
    arr.push(g[4..6]);
    up(x, 0);
    up(arr[0], 0);
    let r = &x;
    var w = r;
    w[1] -= 32;
    print(g);
}

fn main() {
    reported();
    g.append("cdef");
    slots();
    writes();
}
