// error: cannot write through this reference in a par for
// A holder from outside the body handed by value to a function the body
// calls: the function writes through it no more than the body may
// (docs/design/child_threads.md §4.3 R1).
struct H { r: i64& }
fn bump(h: H) { h.r += 1; }
fn main() {
    var n = 0;
    let h = H { r: n };
    par for k in 100000 { bump(h); }
    print(n);
}
