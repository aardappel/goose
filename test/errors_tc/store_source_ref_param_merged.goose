// error: cannot clear a while bx is still used: a reference into it was stored there
// put copies what its parameter points at, which r may make h1 or h2: the
// copy in bx is bounded by both, not traced through the stores of one
// (§3.5, §5.1).
struct Box { s: u8[:] }
fn put(p: Box&, b: Box&) { b = p; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var h2 = Box { s: other[..] };
    var h1 = Box { s: a[..] };
    var bx = Box { s: other[..] };
    let c = a.len > 5;
    var r = &h2;
    if !c { r .= &h1; }
    put(r, bx);
    a.clear();
    a.append("ZZZZZZZZ");
    print(bx.s);
}
