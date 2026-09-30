// error: expected a value of type u8[:]&, got u8[:]
// A reference binding takes no function value's call's value: it is a
// temporary (§9.2), not the slice variable the body names.
var g: u8[>..] = [];
var h: u8[>..] = [];
fn bind<F>() {
    let r: u8[:]& = F();
    r = g[1..];
}
fn main() {
    g.append("xyz");
    h.append("abcd");
    var s = h[..];
    bind() { s };
    print(s.len);
}
