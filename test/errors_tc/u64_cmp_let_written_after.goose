// error: k is bound to a writable reference here, through which it may become negative, while the comparison with a u64 at
// error: u64_cmp_let_written_after.goose:11 relies on its value being non-negative
// §6.1, §4.4: the comparison comes first, but a writable reference to k
// anywhere is enough: in a loop, or through a nested function, a write placed
// later can run first.

fn setneg(r: i64&) { r = -1; }
fn main() {
    let k = 5;
    let u: u64 = 3;
    print(u < k);
    setneg(k);
    print(k);
}
