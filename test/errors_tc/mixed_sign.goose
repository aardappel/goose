// Same-width signed/unsigned operands have no common type (§6.3).
fn main() {
    var a: u32 = 1;
    var b: i32 = 2;
    print(a + b);
}
