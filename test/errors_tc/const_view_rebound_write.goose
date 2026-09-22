// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// A `var` of type const u8[:] is re-sliced as a whole, and its bytes stay
// read-only through it wherever it was re-pointed (§9.5).
fn main() {
    var buf: u8[>..] = "abc";
    var view: const u8[:] = buf[..];
    view = view[1..];
    view[0] = 'X';
    print(buf);
}
