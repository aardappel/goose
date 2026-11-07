// error: cannot assign total in a par for: total is a variable of the enclosing function
// R1 (docs/design/child_threads.md §4.3): what the body sees from outside it
// is read-only there, but for a reduction.
fn main() {
    var total = 0;
    let xs = [1, 2, 3];
    par for k in 3 { total = total + xs[k]; }
    print(total);
}
