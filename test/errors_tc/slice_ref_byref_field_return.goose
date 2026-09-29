// error: returning a reference rooted in st, which dies with this function
// A slice field bound by reference names the field, which lies in st
// (§3.8, §4.1), not what the slice in it views.
struct S { f: u8[:] }
var g: u8[>..] = [];
fn f() -> u8[:]& {
    var st = S { f: g[0..1] };
    let r: u8[:]& = st.f;
    return r;
}
fn main() {
    g.append("abc");
    let x = f();
    print(x);
}
