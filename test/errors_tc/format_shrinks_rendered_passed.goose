// error: cannot call show: it shrinks G while x is still used, and x may refer into G (§5.2)
// A function rendering its parameter in place around an overload that
// shrinks G passes the pair to its callers, and this one passes an element
// of G (§3.7, §5.2).
struct T { n: i64 }
struct W { t: T, k: i64 }
var G: W[>..<] = [W { T { 1 }, 2 }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(W { T { 7 }, 8 });
    out.append("T");
}
fn show(x: W&) { print(x); }
fn main() {
    show(G[0]);
}
