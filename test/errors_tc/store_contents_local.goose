// error: storing a reference rooted at buf, which does not outlive the destination
// h holds static data and, after the push, a view of buf, which the global
// g outlives (§9.2, §9.5).
var g: const u8[:] = "";
fn f() {
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = ["static"];
    if buf.len > 0 { h.push(buf[..]); }
    g = h[h.len - 1];
}
fn clobber() { var z: u8[..8] = "QQQQQQQQ"; print(z.len); }
fn main() { f(); clobber(); print(g); }
