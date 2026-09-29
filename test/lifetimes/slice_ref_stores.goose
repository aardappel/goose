// A store through a reference to a slice assigns the slot it names (§3.8):
// a slice variable there is bound as its own assignment would bind it, a
// `let` included (§4.4), and a call through whose parameter the store is
// made binds the variable the argument names. Only a slice variable a
// reference has been made to may be the slot, and only a slot may be: a
// slice's elements are not one, nor what a reference to one leads to.
struct H { r: u8[:]& }
struct V { v: (u8[:])[:] }
var g1: u8[>..] = [];
var g2: u8[>..] = [];

fn swap(p: u8[:]&, q: u8[:]&) {
    let t = p[..];
    p = q[..];
    q = t;
}

fn put_elem(h: V&, x: u8[:]) { h.v[0] = x; }

fn first_elem(p: (u8[:])[:]&, x: u8[:]) {
    let r: u8[:]& = p[0];
    r = x;
}

recursive fn skip(p: u8[:]&, n: i64) {
    if n > 0 {
        p = p[1..];
        skip(p, n - 1);
    }
}

// A `let` written through a reference, then read directly and through
// another reference to its slot; two slots a callee swaps, each given what
// the other held.
fn written() {
    let s = g1[0..5];
    let r = &s;
    r = g1[6..11];
    let r2: u8[:]& = s;
    print(s, " ", r2);
    var x = g1[0..5];
    var y = g2[..];
    swap(x, y);
    print(x, " ", y);
}

// A nested function's parameter may name u, which a reference has been made
// to, and not t; nor may the reference h holds.
fn named() {
    var loc: u8[>..] = [];
    loc.append("local");
    var u = loc[..];
    let t = g1[..];
    fn set(p: u8[:]&) { p = loc[1..]; }
    set(u);
    let h = H { r: u };
    h.r = loc[2..];
    print(u, " ", t.len);
}

// What a nested function loads through its parameter is what the slot held:
// storing it back re-binds nothing.
fn advanced() {
    var cur = g2[..];
    fn adv(p: u8[:]&) { p = p[1..]; }
    adv(cur);
    adv(cur);
    skip(cur, 1);
    print(cur);
}

// An element of a slice of slices is no slice variable's slot, nor is what
// a reference to the slice's slot leads to.
fn elements() {
    var loc: u8[>..] = [];
    loc.append("local");
    var near = loc[..];
    let rn = &near;
    var views: (u8[:])[>..] = [];
    views.push(g1[0..1]);
    var hv = V { v: views[..] };
    put_elem(hv, g1[1..3]);
    print(views[0], " ", rn);
}

fn element_slots() {
    var views: (u8[:])[>..] = [];
    views.push(g1[0..1]);
    var vs = views[..];
    first_elem(vs, g1[3..5]);
    print(views[0], " ", vs.len);
}

fn main() {
    g1.append("hello world");
    g2.append("goose");
    written();
    named();
    advanced();
    elements();
    element_slots();
}
