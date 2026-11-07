// error: cannot write through this reference in a par for
// An array of references from outside the body handed by value to a
// function the body calls (docs/design/child_threads.md §4.3 R1).
fn bump(rs: i64&[1]) { rs[0] += 1; }
fn main() {
    var n = 0;
    let refs: i64&[1] = [n];
    par for k in 100000 { bump(refs); }
    print(n);
}
