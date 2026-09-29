// runtime-debug: a slice stored into a slot leaves no length facts about the slot behind, however the store reaches it.
recursive fn shorten(p: i64[:]&, a: i64[>..]&) { p = a[0..1]; }
recursive fn pass_on(p: i64[:]&, a: i64[>..]&) { shorten(p, a); }
var ga: i64[>..] = [1, 2, 3];
var gs: i64[:] = ga[..];
recursive fn shorten_global() { shorten(gs, ga); }
fn twice<F: fn()>() { F(); F(); }

// A slice stored through one parameter leaves the length of the array
// another one refers to alone.
recursive fn store_keeps(p: i64[:]&, q: i64[>..]&) {
    if q.len > 0 {
        p = q[0..1];
        print(q[q.len - 1]); // bce:elide
    }
}

// Each slice below holds one element when it is indexed, while the length
// it had before (n) says three: the checks stay, and pass.
fn through_explicit_ref() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    let n = s.len;
    r = a[0..1];
    if n > 0 { print(s[0]); } // bce:keep
    // The array the slices view keeps its length.
    if n > 2 { print(a[2]); } // bce:elide
}

fn through_bound_ref() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r: i64[:]& = s;
    let n = s.len;
    r = a[0..1];
    if n > 0 { print(s[0]); } // bce:keep
}

fn into_variable_with_ref() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    let n = r.len;
    s = a[0..1];
    if n > 0 { print(r[0]); } // bce:keep
}

fn through_optional() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    var o: i64[:]? = null;
    o .= &s;
    let n = s.len;
    if o { o = a[0..1]; }
    if n > 0 { print(s[0]); } // bce:keep
}

fn in_callees() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    var n = s.len;
    shorten(s, a);
    if n > 0 { print(s[0]); } // bce:keep
    s = a[..];
    n = s.len;
    shorten(r, a);
    if n > 0 { print(s[0]); } // bce:keep
    s = a[..];
    n = r.len;
    pass_on(s, a);
    if n > 0 { print(r[0]); } // bce:keep
}

fn in_nested_bodies() {
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    fn cut() { r = a[0..1]; }
    var n = s.len;
    cut();
    if n > 0 { print(s[0]); } // bce:keep
    s = a[..];
    n = s.len;
    twice() { r = a[0..1]; };
    if n > 0 { print(s[0]); } // bce:keep
}

// A for loop re-reads the slot it iterates through a reference each
// iteration unless nothing in the body can store into it.
fn iterated() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [7];
    var s = a[..];
    let r = &s;
    for x in r {
        print(x);
        s = b[0..1];
    }
}

fn main() {
    var q: i64[>..] = [4, 5, 6];
    var t = q[..];
    store_keeps(t, q);
    through_explicit_ref();
    through_bound_ref();
    into_variable_with_ref();
    through_optional();
    in_callees();
    in_nested_bodies();
    iterated();
    let n = gs.len;
    shorten_global();
    if n > 0 { print(gs[0]); } // bce:keep
    var a: i64[>..] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    let m = s.len;
    r = a[0..1];
    if m > 2 { print(s[2]); } // bce:keep
}
