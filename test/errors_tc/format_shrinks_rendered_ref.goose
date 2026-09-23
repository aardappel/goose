// error: cannot call format, which shrinks G: print runs it in the middle of rendering r, which may refer into G (§5.2)
// A reference argument renders its pointee where it lies: in G (§3.7, §5.2).
struct T { n: i64 }
struct W { t: T, k: i64 }
var G: W[>..<] = [W { T { 1 }, 2 }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(W { T { 7 }, 8 });
    out.append("T");
}
fn main() {
    let r: W& = G[0];
    print(r);
}
