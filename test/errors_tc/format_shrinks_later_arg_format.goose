// error: cannot call format, which shrinks G while s (bound at
// And in format, whose receiver comes before every argument rendered into it
// (§3.7, §5.2).
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
