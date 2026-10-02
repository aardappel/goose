// error: returning a reference rooted in buf, which dies with this function
// h holds static data and, after the push, a view of buf, which dies with
// f (§9.2, §9.5).
fn f() -> const u8[:] {
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = ["static"];
    if buf.len > 0 { h.push(buf[..]); }
    return h[h.len - 1];
}
fn clobber() { var z: u8[..8] = "QQQQQQQQ"; print(z.len); }
fn main() { let s = f(); clobber(); print(s); }
