// error: cannot call walk, which shrinks s while mine
// error: is still used: it may refer into s
// A call back into the cycle is taken to be balanced only until the cycle is
// checked (§5.2): the pop after the resize makes walk unbalanced, so the
// deeper activation's pop frees the element mine views.
recursive fn walk(s: i64[>..<]&, d: i64) -> i64 {
    let mark = s.len;
    s.push(d);
    let mine = s[mark..];
    var t = 0;
    if d < 3 { t = walk(s, d + 1); }
    t += mine[0];
    s.resize(mark);
    if d == 3 { s.pop(); }
    return t;
}
fn main() {
    var s: i64[>..<] = [8];
    print(walk(s, 1));
}
