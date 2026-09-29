// error: cannot clear a while bx is still used: a reference into it was stored there
// A holder loaded through a reference is a copy of what h1 holds, not a
// reference into h1: h1's stores say where its slice points (§3.5, §5.1).
struct Box { s: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var h1 = Box { s: a[..] };
    var bx = Box { s: other[..] };
    let r = &h1;
    bx = r;
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
