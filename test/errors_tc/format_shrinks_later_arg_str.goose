// error: cannot call format, which shrinks G while s is still used
// str renders its arguments in order, here over a grow-only global: s[0] is
// read after the overload has cleared G (§3.7, §5.1).
struct T { n: i64 }
var G: i64[>..] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    out.append("T");
}
fn main() {
    let s = G[..];
    let x = str(T { 1 }, s[0]);
    print(x);
}
