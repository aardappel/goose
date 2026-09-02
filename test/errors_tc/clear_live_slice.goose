// A slice into a grow-only local is live at the clear (§5.1).
fn main() {
    var buf: u8[>..] = [];
    buf.append("ab");
    let s = buf[0..2];
    buf.clear();
    print(s.len);
}
