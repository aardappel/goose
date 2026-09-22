// error: cannot call f: it may shrink a while h is still used, and h may refer into a
// hv holds a view of a, and g's merged root only bounds what it points at,
// which may be a, declared further out than b (§9.2): the slice h's holder
// holds still reads a after f clears it and appends (§5.1).
struct H { s: u8[:] }
fn f(h: H&, xs: u8[>..]&) {
    xs.clear();
    xs.append("zz");
    print(h.s);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abcd");
    var hv = H { a[..] };
    {
        var b: u8[>..] = [];
        let g: u8[>..]& = if a.len > 0 { a } else { b };
        f(hv, g);
    }
    print(a.len);
}
