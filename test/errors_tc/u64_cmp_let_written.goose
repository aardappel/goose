// error: comparing u64 with i64 relies on k being non-negative, but a writable reference bound to k at
// error: u64_cmp_let_written.goose:10 may make it negative
// §6.1, §4.4: a `let` is never reassigned, but setneg writes k through a
// reference, after which it is -1, which the unsigned comparison would take
// for u64.max.

fn setneg(r: i64&) { r = -1; }
fn main() {
    let k = 5;
    setneg(k);
    let u: u64 = 3;
    print(k, " ", u < k);
}
