// error: cannot call across, which shrinks s while v
// error: is still used: it may refer into s
// across was checked inside the cycle through down, on the assumption that
// down's calls back are balanced; down pops, so across is not balanced
// either when main calls it later (§5.2).
recursive fn down(s: i64[>..<]&, n: i64) {
    if n > 0 { across(s, n - 1); }
    if n == 1 { s.pop(); }
}
recursive fn across(s: i64[>..<]&, n: i64) {
    down(s, n);
}
fn main() {
    var s: i64[>..<] = [1, 2, 3];
    down(s, 0);
    let v = s[..];
    across(s, 2);
    print(v[2]);
}
