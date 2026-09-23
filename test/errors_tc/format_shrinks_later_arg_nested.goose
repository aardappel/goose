// error: cannot call format, which shrinks G while s (bound at
// The overload runs inside a str print renders first: print's own later
// argument s is read after it (§3.7, §5.2).
struct T { n: i64 }
var G: i64[>..<] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(9);
    out.append("T");
}
fn main() {
    let s = G[..];
    print(str(T { 1 }), s);
}
