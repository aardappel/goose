// error: cannot call walk, which shrinks s while mine
// error: is still used: it may refer into s
// What a function of the cycle calls after the call back counts too (§5.2):
// trim is not balanced, so neither is walk.
fn trim(s: i64[>..<]&) { s.pop(); }
recursive fn walk(s: i64[>..<]&, d: i64) -> i64 {
    let mark = s.len;
    s.push(d);
    let mine = s[mark..];
    var t = 0;
    if d < 3 { t = walk(s, d + 1); }
    t += mine[0];
    trim(s);
    return t;
}
fn main() {
    var s: i64[>..<] = [8];
    print(walk(s, 1));
}
