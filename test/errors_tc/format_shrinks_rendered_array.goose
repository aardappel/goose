// error: cannot call format, which shrinks G: print runs it in the middle of rendering G, which may refer into G (§5.2)
// Rendering an array runs the overload of each element in turn, over the
// elements it counted before the first: those after it are gone once the
// overload clears the array (§3.7, §5.2).
struct T { n: i64 }
var G: T[>..<] = [T { 1 }, T { 2 }];
fn format(out: u8[>..]&, t: T) {
    G.clear();
    format(out, t.n);
}
fn main() {
    print(G);
}
