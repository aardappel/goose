// error: cannot call format, which shrinks G while s (bound at
// error: is still used: it may refer into G (§5.2)
// print renders its arguments in order, each evaluated as its turn comes:
// s is read after T's overload has cleared G and pushed 9 (§3.7, §5.2).
struct T { n: i64 }
var G: i64[>..<] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(9);
    out.append("T");
}
fn main() {
    let s = G[..];
    print(T { 1 }, s);
}
