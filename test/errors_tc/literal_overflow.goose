// error: constant 300 does not fit u8
// A constant operand must fit the other operand's type (§6.1).
fn main() {
    var a: u8 = 1;
    print(a + 300);
}
