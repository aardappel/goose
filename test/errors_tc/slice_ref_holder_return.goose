// error: returning a reference rooted in s, which dies with this function
// wrap's parameter names the caller's slice variable s (§4.1), and so does
// the reference the Named it returns holds (§9.2).
struct Named { rs: u8[:]& }
var g: u8[>..] = [];
fn wrap(p: u8[:]&) -> Named { return Named { rs: p }; }
fn f() -> Named {
    var s = g[0..1];
    let n = wrap(s);
    return n;
}
fn main() {
    g.append("abc");
    let n = f();
    print(n.rs);
}
