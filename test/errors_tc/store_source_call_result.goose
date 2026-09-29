// error: cannot clear a while bx is still used: a reference into it was stored there
// first returns a slice read out of what its argument views, which hb's
// slice does: that the argument was read out of hb says nothing about
// where the result was read out of (§3.5, §5.1).
struct Box { s: u8[:] }
struct HB { bs: Box[:] }
fn first(p: Box[:]) -> u8[:] { return p[0].s; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr = [Box { s: a[..] }];
    var arr2 = [Box { s: other[..] }];
    var hb = HB { bs: arr2[..] };
    let c = a.len > 2;
    if c { hb.bs = arr[..]; }
    var bx = Box { s: other[..] };
    bx.s = first(hb.bs);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
