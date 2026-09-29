// error: returning a reference rooted in buf, which dies with this function
// take's result views what cur holds, buf's elements, which die with word
// (§9.2).
fn take(p: u8[:]&, n: i64) -> u8[:] {
    let r = p[0..n];
    p = p[n..];
    return r;
}
fn word() -> u8[:] {
    var buf: u8[8] = [104, 101, 108, 108, 111, 0, 0, 0];
    var cur = buf[..];
    return take(cur, 5);
}
fn main() { print(word()); }
