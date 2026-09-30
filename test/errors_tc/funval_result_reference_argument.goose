// error: no matching overload for call to set
// A function value's call's value is a copy of its body's, a temporary as
// any call's result is (§9.2), even where the body names a slice variable:
// no storage for a reference parameter to bind (§4.1), as set(get()) is not.
var g: u8[>..] = [];
var h: u8[>..] = [];
fn set(p: u8[:]&) { p = g[1..]; }
fn call_set<F>() { set(F()); }
fn main() {
    g.append("xyz");
    h.append("abcd");
    var s = h[..];
    call_set() { s };
    print(s.len);
}
