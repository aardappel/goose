// error: cannot pop buf while s is still used
// A slice variable bound from a grow-only array views it (§3.10, §5.1).
fn main() {
    var buf: u8[>..] = [];
    buf.push(65);
    let s: const u8[:] = buf;
    buf.pop();
    print(s);
}
