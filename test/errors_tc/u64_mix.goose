// u64 does not widen into any signed type (§6.3).
fn main() {
    var a: u64 = 1;
    var b: i64 = 2;
    print(a + b);
}
