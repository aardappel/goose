// error: cannot clear a while bx is still used: a reference into it was stored there
// bs was read out of hb, whose stores say where its slices point, not what
// the elements they view hold: the copy in bx is bounded by arr1 and arr2,
// the arrays bs may view (§3.5, §3.6, §5.1).
struct Box { s: u8[:] }
struct HB { bs: Box[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr1 = [Box { s: a[..] }];
    var arr2 = [Box { s: other[..] }];
    var hb = HB { bs: arr2[..] };
    let c = a.len > 2;
    if c { hb.bs = arr1[..]; }
    let bs = hb.bs;
    var bx = bs[0];
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
