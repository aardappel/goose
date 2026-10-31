// At an if/else join a value keeps what both branches imply about it, not
// only the facts both state word for word: the value of an if expression,
// and a variable one branch assigns, which the other knows by its earlier
// value. A clamp to the last index is the usual shape.

import std;

// k0 on one side, xs.len - 1 >= 0 on the other.
fn clamped_let(xs: f64[:], k0: i64) -> f64 {
    guard xs.len > 0 else { return 0.0; }
    assert(k0 >= 0);
    let k = if k0 < xs.len { k0 } else { xs.len - 1 };
    return xs[k];                              // bce:elide
}

// k <= last on both paths: assigned on one, compared on the other.
fn clamped_var(xs: f64[:], k0: i64) -> f64 {
    guard xs.len > 0 else { return 0.0; }
    let last = xs.len - 1;
    var k = k0;
    assert(k >= 0);
    if k > last { k = last; }
    return xs[k];                              // bce:elide
}

// Assigned on both, from values each branch bounds its own way.
fn either(xs: f64[:], a: i64, c: bool) -> f64 {
    guard a >= 0 && a < xs.len - 2 else { return 0.0; }
    var k = 0;
    if c { k = a + 2; } else { k = a; }
    return xs[k];                              // bce:elide
}

// A chain: every arm in range.
fn chain(xs: f64[:], k0: i64) -> f64 {
    guard xs.len > 1 else { return 0.0; }
    let k = if k0 < 0 { 0 } else if k0 >= xs.len { xs.len - 1 } else { k0 };
    return xs[k];                              // bce:elide
}

// One arm past the end.
fn past_end(xs: f64[:], k0: i64) -> f64 {
    guard k0 >= 0 && k0 < xs.len else { return 0.0; }
    let k = if k0 > 0 { k0 } else { xs.len };
    return xs[k];                              // bce:keep
}

// A deferred body runs after a block's value is read: here it moves x into
// range, but k is still the old x.
fn deferred(xs: f64[:], z: i64) -> f64 {
    var x = 100;
    let k = { defer { x = z; } x };
    guard x >= 0 && x < xs.len else { return 0.0; }
    return xs[k];                              // bce:keep
}

fn main() {
    let xs: f64[>..] = [1.0, 2.0, 3.0, 4.0];
    var ks: i64[>..] = [9, 1, 0];
    var cs: bool[>..] = [true];
    print(clamped_let(xs, ks[0]), " ", clamped_var(xs, ks[0]), " ",
          either(xs, ks[1], cs[0]), " ", chain(xs, ks[0]), " ", past_end(xs, ks[1]));
    print(deferred(xs, ks[0]));
}
