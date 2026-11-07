// error: cannot write through this reference in a par for
// A reference to a copy of a holder from outside the body, handed to a
// function the body calls (docs/design/child_threads.md §4.3 R1).
struct H { r: i64& }
fn bump(h: H&) { h.r += 1; }
fn main() {
    var n = 0;
    let h = H { r: n };
    par for k in 100000 { var copy = h; bump(copy); }
    print(n);
}
