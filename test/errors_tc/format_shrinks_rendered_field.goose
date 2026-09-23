// error: cannot call format, which shrinks G: print runs it in the middle of rendering w, which may refer into G (§5.1)
// The rest of a struct is rendered around its field's overload: w.s, a
// view of G stored in w, is read after G is cleared (§3.7, §5.1).
struct T { n: i64 }
struct W { t: T, s: i64[:] }
var G: i64[>..] = [1, 2, 3];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(9);
    out.append("T");
}
fn main() {
    let w = W { T { 1 }, G[..] };
    print(w);
}
