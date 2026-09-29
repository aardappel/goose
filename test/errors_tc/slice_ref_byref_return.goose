// error: returning a reference rooted in s, which dies with this function
// A slice variable bound by reference names the variable itself, as `&s`
// does (§3.8, §4.1), not what its slice views: r leads into f's frame,
// wherever s points.
var g: u8[>..] = [];
fn f() -> u8[:]& {
    var s = g[0..1];
    let r: u8[:]& = s;
    return r;
}
fn main() {
    g.append("abc");
    let x = f();
    print(x);
}
