// error: cannot call format, which shrinks G: print runs it in the middle of rendering G[0].ts, which may refer into G (§5.2)
// A fixed-size array is rendered where it lies, each element's overload in
// turn: ts[1] is read after the first one clears G (§3.7, §5.2).
struct T { n: i64 }
struct W { ts: T[2] }
var G: W[>..<] = [W { [T { 1 }, T { 2 }] }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(W { [T { 7 }, T { 8 }] });
    format(out, "T", t.n);
}
fn main() {
    print(G[0].ts);
}
