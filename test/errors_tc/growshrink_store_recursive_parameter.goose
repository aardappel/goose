// error: storing a reference into s, which holds a grow-shrink array
// A parameter whose argument is a back edge's result views G, as the
// cycle's returns give it (§7.8), so wrap's literal may not hold it (§5.2).
struct H { s: const i64[:] }
var G: i64[>..<] = [1, 2, 3];
fn f(h: H) {
    G.clear();
    G.push(9);
    print(h.s);
}
fn wrap(s: const i64[:]) -> H { H { s } }
recursive fn rec(n: i64) -> i64[:] {
    if n > 0 { f(wrap(rec(n - 1))); }
    let r = if n > 5 { G[1..] } else { G[..] };
    return r;
}
fn main() { print(rec(1)); }
