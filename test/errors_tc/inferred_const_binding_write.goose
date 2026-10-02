// error: cannot assign through this path
// An un-annotated variable initialized with a read-only slice has a const
// type (§9.5), so it stays read-only after a writable slice is assigned.
var buf: u8[] = [1, 2, 3];
fn poke(s: u8[:]) {
    var t = s;
    t = buf[..];
    t[0] = 7;
}
fn main() { poke("abc"); }
