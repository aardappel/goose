// error: may point at total, a reduction of this par for
// A reference read out of storage in the body that may point at a reduction
// reads the variable (docs/design/child_threads.md §4.6).
fn main() {
    var total = 1;
    let refs: i64&[1] = [total];
    par for k in 8 {
        let r = refs[0];
        total += r;
    }
    print(total);
}
