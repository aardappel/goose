// error: cannot write through this reference in a par for
// A copy of a holder from outside the body holds what that holder does: a
// reference to a variable the body sees read-only (docs/design/child_threads.md
// §4.3 R1).
struct H { r: i64& }
fn main() {
    var n = 0;
    let h = H { r: n };
    par for k in 100000 { var copy = h; copy.r += 1; }
    print(n);
}
