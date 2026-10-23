// Scalar fields read through a reference in loops, which codegen keeps in
// locals (CacheFieldReads). Each loop prints what it read, so a local kept
// past a store to its field prints a stale value: a store by a call or what
// it calls, through another reference, of a whole value, through a scalar
// reference, by an inlined helper, a block or a `format` overload, or after
// rebinding the reference.

struct Ctr { limit: i64, step: i64, scale: f64, on: bool }
struct Outer { tag: i64, inner: Ctr }

var gc = Ctr { limit: 3, step: 1, scale: 1.5, on: true };

// Out of line (`recursive`), so that the loops below call them.
recursive fn bump(c: Ctr&) { c.limit += 10; }
recursive fn bump_via(c: Ctr&) { bump(c); }
recursive fn bump_global() { gc.limit += 100; }
recursive fn reset_outer(o: Outer&) { o = Outer { tag: 0, inner: Ctr { limit: 5, step: 2, scale: 2.0, on: false } }; }
recursive fn pure_sum(a: i64, b: i64) -> i64 { a + b }
recursive fn countdown(c: Ctr&, n: i64) { if n == 0 { c.step += 1; } else { countdown(c, n - 1); } }

// Inlined: their bodies are in the loop.
fn read_limit(c: Ctr&) -> i64 { c.limit }
fn set_limit(c: Ctr&, v: i64) { c.limit = v; }

// Read-only in the loop: kept in a local, read again after the call that
// stores to it. The call that stores nothing leaves the local as it is.
fn rare_call(c: Ctr&) -> i64 {
    var s = 0;
    for i in 8 {
        s = pure_sum(s * 3, c.limit) % 1000003;
        if i % 3 == 2 { bump(c); }
        s += read_limit(c) * c.step;   // The inlined helper's parameter stands for c.
    }
    return s;
}

// The call stores through what it calls in turn.
fn transitive_call(c: Ctr&) -> i64 {
    var s = 0;
    for i in 5 {
        s = s * 7 + c.limit;
        if i == 1 { bump_via(c); }
        if i == 3 { countdown(c, 2); }
        s += c.step;
    }
    return s;
}

// The call stores to a global the reference refers to.
fn global_call(c: Ctr&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 5 + c.limit;
        if i == 1 { bump_global(); }
    }
    return s;
}

// The same field stored through another reference, which may be the same
// struct: not kept.
fn other_ref(a: Ctr&, b: Ctr&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 10 + a.limit;
        b.limit += 1;
    }
    return s;
}

// A whole value holding the struct, stored in the loop and by a call.
fn whole_values(o: Outer&, other: Outer&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 10 + o.inner.limit;
        if i == 1 { o.inner = other.inner; }
    }
    for i in 3 {
        s = s * 10 + o.inner.step;
        if i == 0 { reset_outer(o); }
    }
    return s;
}

// A store through a scalar reference of the field's type.
fn through_scalar(c: Ctr&, p: i64&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 10 + c.limit;
        if i == 1 { p = 7; }
    }
    return s;
}

// An inlined helper and a block store to the field.
fn inlined_store(c: Ctr&) -> i64 {
    var s = 0;
    for i in 4 {
        s = s * 10 + c.limit;
        if i == 1 { set_limit(c, 4); }
    }
    for i in 3 {
        s = s * 10 + c.step;
        twice() { c.step += 1; }
    }
    return s;
}

fn twice<F>() { F(); F(); }

// A rebindable reference, moved to another struct mid-loop.
fn rebound(a: Ctr&, b: Ctr&) -> i64 {
    var r: Ctr& = a;
    var s = 0;
    for i in 4 {
        s = s * 10 + r.limit;
        if i == 1 { r .= b; }
    }
    return s;
}

// Nested loops: the outer loop's local is read again after a call in the
// inner one; float and bool fields.
fn nested(c: Ctr&) -> f64 {
    var s = 0.0;
    var i = 0;
    while i < 3 {
        for j in 3 {
            if c.on { s = s * 2.0 + c.scale; }
            if j == 1 && i == 1 { bump(c); }
        }
        s += c.limit;
        i++;
    }
    return s;
}

// A `format` overload that stores to what the loop reads.
struct Tick { c: Ctr& }
fn format(out: u8[>..]&, t: Tick&) { t.c.limit += 1; format(out, t.c.limit); }

fn formatted(c: Ctr&, t: Tick&) -> i64 {
    var text: u8[>..] = [];
    var s = 0;
    for i in 3 {
        s = s * 100 + c.limit;
        format(text, t);
    }
    print(text);
    return s;
}

fn main() {
    var c = Ctr { limit: 1, step: 2, scale: 0.5, on: true };
    print(rare_call(c), " ", c.limit);
    c = Ctr { limit: 1, step: 2, scale: 0.5, on: true };
    print(transitive_call(c), " ", c.limit, " ", c.step);
    print(global_call(gc), " ", gc.limit);
    var d = Ctr { limit: 1, step: 0, scale: 0.0, on: false };
    print(other_ref(d, d), " ", d.limit);
    var e = Ctr { limit: 1, step: 0, scale: 0.0, on: false };
    print(other_ref(d, e), " ", e.limit);
    var o = Outer { tag: 1, inner: Ctr { limit: 1, step: 3, scale: 0.0, on: true } };
    let p = Outer { tag: 2, inner: Ctr { limit: 8, step: 9, scale: 0.0, on: true } };
    print(whole_values(o, p), " ", o.inner.limit);
    c = Ctr { limit: 1, step: 2, scale: 0.5, on: true };
    print(through_scalar(c, c.limit), " ", c.limit);
    print(inlined_store(c), " ", c.limit, " ", c.step);
    var f = Ctr { limit: 6, step: 0, scale: 0.0, on: false };
    print(rebound(c, f));
    c = Ctr { limit: 1, step: 2, scale: 0.25, on: true };
    print(nested(c), " ", c.limit);
    var t = Tick { c: c };
    print(formatted(c, t), " ", c.limit);
}
