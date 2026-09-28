// error: the branches' constants 1 to 300 do not all fit u8
// A construct of integer constants adapts to another operand's type only
// where every one of them fits it (§6.4).
fn main() {
    var flags: bool[>..] = [true, false];
    var small: u8 = 5;
    print((if flags[1] {1} else {300}) + small);
}
