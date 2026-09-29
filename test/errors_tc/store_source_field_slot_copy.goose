// error: cannot clear a while bx is still used: a reference into it was stored there
// The element put copies lies in arr, which the slice in hb.bs views: hb's
// stores say where its slices point, not what the elements they view hold,
// so the copy in bx is bounded by hb (§3.5, §5.1).
struct Box { s: u8[:] }
struct HB { bs: Box[:] }
fn put(p: Box[:]&, b: Box&) { b = p[0]; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr = [Box { s: a[..] }];
    var hb = HB { bs: arr[..] };
    var bx = Box { s: other[..] };
    put(hb.bs, bx);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
