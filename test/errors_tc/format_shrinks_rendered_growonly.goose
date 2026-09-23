// error: cannot call format, which shrinks G: print runs it in the middle of rendering G[1], which may refer into G (§5.1)
// The same for a grow-only array, whose slots after the overload's clear
// hold its own pushes (§3.7, §5.1).
struct T { n: i64 }
struct W { t: T, k: i64 }
var G: W[>..] = [W { T { 1 }, 2 }, W { T { 3 }, 4 }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(W { T { 5 }, 6 });
    G.push(W { T { 7 }, 8 });
    out.append("T");
}
fn main() {
    print(G[1]);
}
