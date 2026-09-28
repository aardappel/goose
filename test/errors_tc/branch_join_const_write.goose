// error: cannot assign through this path (const, or a read-only instantiation
// A string literal's branch makes the joined slice const (§9.5): a write
// through it would reach the literal where that branch was taken.
fn main() {
    var buf: u8[>..] = [];
    buf.push(65);
    let c = buf.len > 0;
    let s = if c { buf } else { "none" };
    s[0] = 66;
    print(buf);
}
