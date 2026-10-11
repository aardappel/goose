// A completed pop_n proves 0 <= n <= the old length, as a completed pop
// proves the array was non-empty (§9.3), and a constant n is an exact step
// down of the length. Arrays and counts come from loaded values, so each
// function proves only what its own body states.

fn constant_step(l: i64[..8]&) -> i64 {
    guard l.len > 5 else { return 0; }
    l.pop_n(2);
    var s = l[3];                             // bce:elide (the length is at least 4)
    l.pop_n(3);
    s += l[0];                                // bce:elide (and now at least 1)
    if l.len > 1 { s += l[1]; }               // bce:elide
    return s;
}

fn constant_step_short(l: i64[..8]&) -> i64 {
    guard l.len > 5 else { return 0; }
    l.pop_n(5);
    return l[1];                              // bce:keep (the length may be 1)
}

fn count_in_range(l: i64[>..<]&, n: i64) -> i64 {
    let xs: i64[9] = [1, 2, 3, 4, 5, 6, 7, 8, 9];
    guard l.len <= 8 else { return 0; }
    l.pop_n(n);
    return xs[n];                             // bce:elide (0 <= n <= the old length <= 8)
}

fn count_unbounded(l: i64[>..<]&, n: i64) -> i64 {
    let xs: i64[9] = [1, 2, 3, 4, 5, 6, 7, 8, 9];
    l.pop_n(n);
    return xs[n];                             // bce:keep (the old length is unbounded)
}

fn old_length(l: i64[>..<]&) -> i64 {
    let xs: i64[6] = [1, 2, 3, 4, 5, 6];
    let m = l.len;
    guard m <= 8 else { return 0; }
    l.pop_n(3);
    return xs[m - 3];                         // bce:elide (3 <= m <= 8)
}

fn variable_step(l: i64[>..<]&, n: i64) -> i64 {
    let m = l.len;
    guard m > 0 else { return 0; }
    l.pop_n(n);
    var s = 0;
    if l.len > 0 { s += l[l.len - 1]; }       // bce:elide
    return s + l[m - 1];                      // bce:keep (n may be positive)
}

fn filled(n: i64) -> i64[>..<] {
    var a: i64[>..<] = [];
    for i in n { a.push(i + 1); }
    return a;
}

fn main() {
    let ks = filled(9);
    var a: i64[..8] = [];
    for i in ks[6] { a.push(i + 1); }         // 7 elements, opaque to the analysis
    print(constant_step(a), " ", a.len);      // 7 2
    var a2: i64[..8] = [];
    for i in ks[6] { a2.push(i + 1); }        // 7 elements
    print(constant_step_short(a2), " ", a2.len);  // 2 2
    var b = filled(ks[4]);
    print(count_in_range(b, ks[2]), " ", b.len);  // 4 2
    var c = filled(ks[8]);
    print(count_unbounded(c, ks[0]), " ", c.len); // 2 8
    print(old_length(c), " ", c.len);         // 6 5
    print(variable_step(c, ks[0] - 1), " ", c.len);   // 10 5
}
