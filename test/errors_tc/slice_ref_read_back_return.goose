// error: returning a reference rooted in s, which dies with this function
// h.r names f's slice variable s (§3.8, §4.1), which the read-back of it
// counts among what it may be (§9.5), a global able to hold a slice beside:
// the reference dies with f.
struct Holder { t: u8[:] }
struct H { r: u8[:]& }
var src: u8[>..] = [];
var G = Holder { t: src[..] };
fn f() -> u8[:]& {
    var a: u8[>..] = [];
    a.append("ab");
    var s = a[..];
    let h = H { r: s };
    return h.r;
}
fn main() {
    src.append("xyz");
    let r = f();
    print(r, G.t);
}
