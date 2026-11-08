// error: cannot assign through this path
// A reference to the slice field of a copy of a holder from outside the
// body: the slice it holds views the array from outside the body, which a
// function handed it writes no more than the body may
// (docs/design/child_threads.md §4.3 R1).
struct H { s: i64[:] }
fn bump(s: i64[:]) { s[0] += 1; }
fn main() {
    var xs: i64[4] = [0, 0, 0, 0];
    let h = H { s: xs };
    par for k in 1000000 { var copy = h; bump(&copy.s); }
    print(xs[0]);
}
