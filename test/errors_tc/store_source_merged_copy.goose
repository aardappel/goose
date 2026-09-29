// error: cannot clear a while by is still used: a reference into it was stored there
// put reads bx's slice out of an element of arr, which the argument, read out
// of hb, views: the store bx's contents record is bounded by arr, not read out
// of hb, whose stores say where its slices point. A copy of bx merged with
// bz, which names no one container, is bounded the same way (§3.5, §5.1).
struct Box { s: u8[:] }
struct HB { bs: Box[:] }
fn put(p: Box[:], b: Box&) { b.s = p[0].s; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr = [Box { s: a[..] }];
    var hb = HB { bs: arr[..] };
    var bx = Box { s: other[..] };
    var bz = Box { s: other[..] };
    put(hb.bs, bx);
    let c = a.len > 2;
    var by = if c { bx } else { bz };
    a.clear();
    a.append("ZZZZZZZZ");
    print(by.s);
}
