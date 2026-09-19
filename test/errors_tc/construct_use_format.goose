// error: cannot call format in the value assigned to G: format uses G
// str() renders Q through the program's format overload (§3.7).
struct Q { n: i64 }
var G: u8[>..] = [];
fn format(out: u8[>..]&, q: Q) { format(out, "Q", q.n, G.len); }
fn main() {
    G.append("abc");
    let q = Q { n: 4 };
    G = str(q);
    print(G);
}
