// error: cannot call across, which shrinks s while mine
// error: is still used: it may refer into s
// across was checked before down turned out to pop (§5.2): the call into it
// was judged on down's calls back being balanced, and is judged again once
// down is checked.
recursive fn down(s: i64[>..<]&, n: i64) -> i64 {
    let mark = s.len;
    s.push(n);
    let mine = s[mark..];
    let deeper = across(s, n - 1);
    let got = mine[0] + deeper;
    s.resize(mark);
    if n == 1 { s.pop(); }
    return got;
}
recursive fn across(s: i64[>..<]&, n: i64) -> i64 {
    if n <= 0 { return 0; }
    return down(s, n);
}
fn main() {
    var s: i64[>..<] = [1, 2, 3];
    print(down(s, 3));
}
