// error: storing a reference rooted at src, which does not outlive the destination
// A reference parameter binds the slot of each slice a construct's branches
// name (§4.1), so what the callee stores there must outlive each variable.
fn put(p: u8[:]&, src: u8[:]) { p = src; }
fn main() {
    var h: u8[>..] = [];
    h.append("abcd");
    var s1 = h[..];
    var s2 = h[1..];
    let c = s1.len > s2.len;
    {
        var local: u8[>..] = [];
        local.append("xy");
        put(if c { s1 } else { s2 }, local[..]);
    }
    print(s1, s2);
}
