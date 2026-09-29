// error: k is bound to a writable reference here, through which it may become negative, while the comparison with a u64 at
// error: u64_cmp_let_nested_reader.goose:10 relies on its value being non-negative
// §6.1, §4.4, §7.5: below is checked once, at its first call, and relies on
// k there; its second call runs after setneg has written k.

fn setneg(r: i64&) { r = -1; }
fn main() {
    let k = 5;
    let u: u64 = 3;
    fn below() -> bool { u < k }
    print(below());
    setneg(k);
    print(below());
}
