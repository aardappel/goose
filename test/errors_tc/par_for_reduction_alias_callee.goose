// error: refs may point at total, a reduction of this par for
// A callee handed an array of references to a reduction reads the variable
// through them (docs/design/child_threads.md §4.6).
fn first(rs: i64&[1]) -> i64 { rs[0] }
fn main() {
    var total = 1;
    let refs: i64&[1] = [total];
    par for k in 8 { total += first(refs); }
    print(total);
}
