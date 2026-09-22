// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// error: declare the slot const
// An annotated variable is a slot, and its type says what it holds (§9.5): a
// u8[:] variable is assigned only writable slices, so a write through it
// can never reach a literal.
fn main() {
    var buf: u8[3] = "abc";
    var s: u8[:] = buf[..];
    s = "lit";
    s[0] = 'X';
    print(buf);
}
