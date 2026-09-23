// error: cannot call inner, which shrinks s while mine
// error: is still used: it may refer into s
// inner's cycle ends inside outer's, whose calls back were only assumed
// balanced then (§5.2): outer pops, so the call into inner is not balanced.
recursive fn outer(s: i64[>..<]&, n: i64) -> i64 {
    let mark = s.len;
    s.push(n);
    let mine = s[mark..];
    let got = inner(s, n);
    let t = got + mine[0];
    s.resize(mark);
    if n == 2 { s.pop(); }
    return t;
}
recursive fn inner(s: i64[>..<]&, n: i64) -> i64 {
    if n <= 0 { return 0; }
    let r = inner(s, n - 1);
    return r + outer(s, n - 1);
}
fn main() {
    var s: i64[>..<] = [1, 2, 3];
    print(outer(s, 3));
}
