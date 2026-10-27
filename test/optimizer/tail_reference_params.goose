// Tail-recursion elimination rebinds a reference parameter to the tail
// call's argument and runs the next call in the same activation. Each of the
// first functions passes down a reference into its activation's own storage
// -- a local, a by-value parameter, a copy -- which the recursion keeps alive
// and unchanged under the call, and which the loop would overwrite before
// the next call reads it, so these keep their calls. A leading statement
// keeps base-case inlining away where the body would start with its base
// case.

recursive fn local(n: i64, p: i64&) -> i64 {
    var t = 100;
    if n == 0 { return p; }
    t = p + 1;
    return 1 + local(n - 1, t);
}

// A by-value parameter, which the loop rebinds.
recursive fn param(n: i64, p: i64&) -> i64 {
    let ignored = 0;
    if n == 0 { return p; }
    return 1 + param(n - 1, n);
}

// A callee handing back the reference it was given.
fn pick(x: i64&) -> i64& { return x; }
recursive fn picked(n: i64, p: i64&) -> i64 {
    var t = 100;
    if n == 0 { return p; }
    t = p + 1;
    return 1 + picked(n - 1, pick(t));
}

// A `let` reference to a by-value parameter, and a `let` slice of a local.
recursive fn let_ref(n: i64, p: i64&) -> i64 {
    let ignored = 0;
    if n == 0 { return p; }
    let r: i64& = n;
    return 1 + let_ref(n - 1, r);
}
recursive fn let_slice(n: i64, p: i64&) -> i64 {
    var t = [100, 200];
    if n == 0 { return p; }
    t[0] = p + 1;
    let s: i64[:] = t;
    return 1 + let_slice(n - 1, s[0]);
}

// A match arm's copy of a local's payload.
enum T { Leaf { v: i64 }, Node { v: i64, next: T& } }
var leaf: T = T.Leaf { v: 0 };
recursive fn arm_copy(n: i64, p: i64&) -> i64 {
    let e: T = T.Node { v: p + 1, next: leaf };
    match e {
        Leaf l => { return l.v; },
        Node nd => {
            if n == 0 { return p; }
            return 1 + arm_copy(n - 1, nd.v);
        },
    }
}

// A nested function handing back a local it captures.
fn nested(m: i64) -> i64 {
    recursive fn walk(n: i64, p: i64&) -> i64 {
        var t = 100;
        fn get() -> i64& { return t; }
        if n == 0 { return p; }
        t = p + m;
        return 1 + walk(n - 1, get());
    }
    var x = 5;
    return walk(2, x);
}

// These pass on storage outside the activation, which the loop can carry:
// a node read through a match arm's copy of the parameter's payload, a
// by-value struct parameter's reference, an element of a slice parameter
// through a `let` subslice.
recursive fn list(l: T&) -> i64 {
    match l {
        Leaf f => { return f.v; },
        Node nd => { return nd.v + list(nd.next); },
    }
}
struct S { x: i64, r: i64& }
recursive fn field(n: i64, s: S, p: i64&) -> i64 {
    let ignored = 0;
    if n == 0 { return p; }
    return p + field(n - 1, s, s.r);
}
recursive fn elems(n: i64, xs: i64[:], p: i64&) -> i64 {
    let ignored = 0;
    if n == 0 { return p; }
    let ys = xs[1..];
    return p + elems(n - 1, xs, ys[n - 1]);
}

// Accumulator lowering reads the term added to a self-call's value before the
// call. Here p, read through a reference after the call, sees what the base
// case wrote there, so `sum` and `bits` keep their calls.
fn change(p: i64&) { p = 10; }
recursive fn sum(p: i64&, n: i64) -> i64 {
    // Keep base-case inlining from obscuring the independent TRE path.
    let ignored = 0;
    if n == 0 { change(p); return 0; }
    return sum(p, n - 1) + p;
}
fn setbits(p: u64&) { p = 8; }
recursive fn bits(p: u64&, n: i64) -> u64 {
    let ignored = 0;
    if n == 0 { setbits(p); return 0; }
    return bits(p, n - 1) | p;
}
// An ordinary by-value scalar can still be carried by an accumulator.
recursive fn scalar(p: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return 0; }
    return scalar(p, n - 1) + p;
}

fn main() {
    var x = 5;
    print(local(2, x), " ", param(2, x), " ", picked(2, x));
    print(let_ref(2, x), " ", let_slice(2, x), " ", arm_copy(2, x), " ", nested(10));
    let b: T = T.Node { v: 2, next: leaf };
    let c: T = T.Node { v: 3, next: b };
    var y = 7;
    let s = S { x: 1, r: y };
    let a = [1, 2, 3, 4];
    print(list(c), " ", field(2, s, x), " ", elems(2, a, x));
    var z = 1;
    print(sum(z, 2), " ", z);
    var w: u64 = 1;
    print(bits(w, 2), " ", w);
    print(scalar(3, 4));
}
