// error: a value of type u8[] is frozen at construction
// error: rebuild its container instead
// Variable-class lvalues are frozen at construction (§4.4).
fn main() {
    var s: u8[] = "abc";
    s = "de";
}
