// error: k is bound to a writable reference here, through which it may become negative, while the comparison with a u64 at
// error: u64_cmp_let_nested_written.goose:12 relies on its value being non-negative
// §6.1, §4.4, §7.5: spoil is checked where it is first called, after the
// comparison, and writes the k it names through a reference, which the
// second comparison would then see.

fn setneg(r: i64&) { r = -1; }
fn main() {
    let k = 5;
    fn spoil() { setneg(k); }
    let u: u64 = 3;
    print(u < k);
    spoil();
    print(u < k);
}
