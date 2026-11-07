// error: cannot write through this reference in a par for
// A copy of a global holder holds references to globals, which a par for's
// body sees read-only (docs/design/child_threads.md §4.3 R1).
struct H { r: i64& }
var G = 0;
let GH = H { r: G };
fn main() {
    par for k in 100000 { var c = GH; c.r += 1; }
    print(G);
}
