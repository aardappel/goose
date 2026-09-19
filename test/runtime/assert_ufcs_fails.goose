// x.assert() checks x at run time like assert(x) (§7.1): only a true
// receiver folds away, and a false one aborts at its own line. Expected to
// abort.

fn main() {
    let xs = [3, 1];
    (xs[0] > xs[1]).assert();
    true.assert();
    print("checked");
    false.assert();                      // Aborts.
    print("not reached");
}
