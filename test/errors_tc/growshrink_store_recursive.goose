// error: storing the result of a recursive call whose returned reference's root the cycle's returns do not determine
// error: it may only be passed down
// No scan of rec's returns can name the root of what the back edge returns
// (§7.8), a view into G as it happens, so a literal may not hold it even
// when the literal itself is only passed down (§5.2).
struct H { s: const i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn f(h: H) {
    G.clear();
    G.push(9);
    print(h.s);
}
recursive fn rec(n: i64) -> i64[:] {
    if n > 0 { f(H { rec(n - 1) }); }
    let r = if n > 5 { G[1..] } else { G[..] };
    return r;
}
fn main() { print(rec(1)); }
