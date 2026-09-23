// error: cannot call format, which shrinks G: print runs it in the middle of rendering G[0], which may refer into G (§5.2)
// The struct rendered lies in the array the overload clears, and its field
// k is read after t's overload (§3.7, §5.2).
struct T { n: i64 }
struct W { t: T, k: i64 }
var G: W[>..<] = [W { T { 1 }, 2 }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(W { T { 7 }, 8 });
    out.append("T");
}
fn main() {
    print(G[0]);
}
