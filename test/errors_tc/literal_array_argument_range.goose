// error: no matching overload for call to color
// An array literal's constants must all fit the elements of the parameter
// it adapts to, as a constant must fit its type (§3.1): a negative one and
// one past u8's range leave no candidate; the candidate's line says
// "its constants -1 to 256 do not all fit u8".
fn color(c: u8[4]) -> i64 { c.len }
fn main() {
    print(color([-1, 0, 0, 256]));
}
