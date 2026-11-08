// error: cannot modify through this reference in a par for
// As par_for_holder_copy.goose, by ++ (docs/design/child_threads.md §4.3 R1).
struct H { r: i64& }
fn main() {
    var n = 0;
    let h = H { r: n };
    par for k in 100000 { let copy = h; copy.r++; }
    print(n);
}
