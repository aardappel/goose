// error: cannot clear a while bx is still used: a reference into it was stored there
// The element popped may be arr1's or arr2's, whichever r names, so neither
// one's stores say what bx holds (§3.5, §5.1).
struct Box { s: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var arr1: Box[>..] = [Box { s: a[..] }];
    var arr2: Box[>..] = [Box { s: other[..] }];
    let c = a.len > 5;
    var r: Box[>..]& = arr2;
    if !c { r .= arr1; }
    var bx = r.pop();
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
