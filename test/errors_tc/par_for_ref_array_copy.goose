// error: cannot write through this reference in a par for
// A copy of an array of references from outside the body holds references
// to variables the body sees read-only (docs/design/child_threads.md §4.3 R1).
fn main() {
    var n = 0;
    let refs: i64&[1] = [n];
    par for k in 100000 { var copy = refs; copy[0] += 1; }
    print(n);
}
