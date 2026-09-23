// error: cannot call walk, which shrinks s while mine
// error: is still used: it may refer into s
// walk resizes its parameter's array back to its mark but clears a global,
// which is what main passes it (§5.2): the deepest activation frees the
// element every shallower one views.
var G: i64[>..<] = [];
recursive fn walk(s: i64[>..<]&, d: i64) -> i64 {
    let mark = s.len;
    s.push(d);
    let mine = s[mark..];
    var t = 0;
    if d < 3 { t = walk(s, d + 1); }
    t += mine[0];
    s.resize(mark);
    if d == 3 { G.clear(); }
    return t;
}
fn main() {
    G.push(1);
    print(walk(G, 1));
}
