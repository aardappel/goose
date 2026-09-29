// error: cannot clear a while bx is still used: a reference into it was stored there
// put reads a slice out of an element of what its parameter's slot views:
// the slot is hb's field, whose stores say where the slice in it points,
// not what the elements it views hold (§3.5, §3.6, §5.1).
struct Box { s: u8[:] }
struct HB { bs: Box[:] }
fn put(p: Box[:]&, b: Box&) { b.s = p[0].s; }
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
