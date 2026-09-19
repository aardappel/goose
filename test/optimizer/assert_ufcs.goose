// x.assert() is assert(x) (§7.1) in every pass: a true receiver folds away
// as assert(true) does, and bounds-check elimination takes the receiver's
// facts and ends the path at a false one.

fn at(s: i64[:], k: i64) -> i64 {
    (k >= 0).assert();
    (k < s.len).assert();
    s[k]                                          // bce:elide
}

fn last(s: i64[:]) -> i64 {
    if s.len < 1 { false.assert(); }
    s[s.len - 1]                                  // bce:elide
}

fn main() {
    let b = true;
    b.assert();
    true.assert();
    let xs = [10, 20, 30];
    // Indices loaded from array contents, which the analysis does not track.
    let ks = [2, 0];
    (xs[ks[0]] > xs[ks[1]]).assert();
    print(at(xs, ks[0]), " ", last(xs[ks[1]..]));
}
