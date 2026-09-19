// x.assert() is assert(x) (§7.1) in every pass: a true receiver folds away
// as assert(true) does, bounds-check elimination takes the receiver's facts
// and ends the path at a false one, and an optional receiver is narrowed
// for the rest of its block (§3.8). An optional receiver reaches any other
// function taking one as its first argument too.

fn at(s: i64[:], k: i64) -> i64 {
    (k >= 0).assert();
    (k < s.len).assert();
    s[k]                                          // bce:elide
}

fn last(s: i64[:]) -> i64 {
    if s.len < 1 { false.assert(); }
    s[s.len - 1]                                  // bce:elide
}

fn plus1(o: i64?) -> i64 {
    o.assert();
    o + 1
}

fn sum(p: i64?, q: i64?) -> i64 {
    (p != null && q != null).assert();
    p + q
}

fn or0(o: i64?) -> i64 {
    if o { o } else { 0 }
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
    var x = 41;
    var y = 1;
    print(plus1(x), " ", sum(x, y));
    var r: i64? = null;
    print(r.or0());
    if xs[ks[1]] > 0 { r .= &x; }
    r.assert();
    print(r + 1, " ", r.or0());
}
