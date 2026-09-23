// error: cannot call format, which shrinks G while h is still used: a reference into it was stored there at
// A later argument naming a holder of a view of the grow-only G uses the
// view after the overload clears G (§3.7, §5.1).
struct T { n: i64 }
struct H { s: i64[:] }
var G: i64[>..] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    out.append("T");
}
fn main() {
    let h = H { G[..] };
    print(T { 1 }, h.s);
}
