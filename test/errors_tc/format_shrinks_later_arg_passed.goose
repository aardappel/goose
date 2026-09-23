// error: cannot call show: it shrinks G while s is still used, and s may refer into G (§5.2)
// A function printing its parameter after an overload that shrinks G passes
// the pair to its callers, and this one makes s a view of G (§3.7, §5.2).
struct T { n: i64 }
var G: i64[>..<] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(9);
    out.append("T");
}
fn show(s: i64[:]) { print(T { 1 }, s); }
fn main() {
    show(G[..]);
}
