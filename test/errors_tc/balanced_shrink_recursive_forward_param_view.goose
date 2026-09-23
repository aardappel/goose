// error: cannot call down: it shrinks G while v is still used, and v may refer into G
// The call into across, a finished member of the cycle, is judged on the
// assumption that its call back into down is balanced, so it records no pair
// for v; once down turns out to clear G, the pair is recorded after all
// (§5.1, §5.2).
var G: i64[>..<] = [1, 2, 3];
recursive fn down(v: i64[:], n: i64) -> i64 {
    var t = 0;
    if n > 0 { t = across(v, n - 1); }
    t += v[0];
    if n == 1 {
        G.clear();
        G.push(77);
    }
    return t;
}
recursive fn across(v: i64[:], n: i64) -> i64 {
    return down(v, n);
}
fn main() {
    print(down(G[..], 2));
}
