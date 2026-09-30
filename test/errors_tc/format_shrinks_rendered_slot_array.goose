// error: cannot call format, which shrinks G: print runs it in the middle of rendering h, which may refer into G (§5.2)
// A reference to a whole grow-shrink array may be stored: h.r leads to G,
// whose elements are rendered over the count read before the first
// overload, which clears G (§3.7, §5.2).
struct A { v: i64 }
struct H { n: i64, r: A[>..<]& }
var G: A[>..<] = [A { v: 1 }, A { v: 2 }];
fn format(out: u8[>..]&, a: A) {
    G.clear();
    format(out, a.v);
}
fn main() {
    let h = H { n: 1, r: G };
    print(h);
}
