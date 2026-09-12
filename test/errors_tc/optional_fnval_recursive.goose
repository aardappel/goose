// A recursive call may rebind every optional its function values can reach.
recursive fn rec<F, G>(k: i64) {
    if k > 0 { rec(k - 1, F, G); }
    G();
    F();
}
fn main() {
    var n = 41;
    var opt: i64? = n;
    fn use() { print(opt + 1); }
    fn clear() { opt .= null; }
    assert(opt);
    rec(1, clear, use);
}
