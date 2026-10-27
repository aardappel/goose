// error: cannot call format, which shrinks G while s (bound at
// format renders its arguments in order after its receiver, each as its turn
// comes: s is read after T's overload has cleared G (§3.7, §5.2).
struct T { n: i64 }
var G: i64[>..<] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    out.append("T");
}
fn main() {
    var b: u8[>..] = [];
    let s = G[..];
    b.format(T { 1 }, " ", s);
    print(b);
}
