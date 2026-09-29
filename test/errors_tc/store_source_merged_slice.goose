// error: cannot clear a while bx is still used: a reference into it was stored there
// The element copied may lie in arr1 or arr2, so neither one's stores say
// what bx holds: each only bounds it (§3.5, §5.1).
struct Box { s: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr1 = [Box { s: a[..] }];
    var arr2 = [Box { s: other[..] }];
    let c = a.len > 5;
    let bs = if c { arr2[..] } else { arr1[..] };
    var bx = bs[0];
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
