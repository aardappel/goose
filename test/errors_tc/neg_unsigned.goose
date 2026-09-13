// error: cannot negate a value of unsigned type u32 (convert with `as`)
// Unsigned values cannot be negated (§6.2).
fn main() {
    var a: u32 = 1;
    print(-a);
}
