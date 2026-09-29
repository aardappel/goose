// A copy's store record names the one container it came out of, where there
// is one, so a shrink of an array beside it passes: each of bx, by and bz
// ends up with a view of a, copied out of h, and clearing other, at the same
// depth, leaves them valid (§3.5, §5.1).
struct Box { s: u8[:] }
fn put(p: u8[:]&, b: Box&) { b.s = p[0..1]; }
fn take(p: Box&, b: Box&) { b = p; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var other: u8[>..] = [];
    other.append("xyz");
    var spare: u8[>..] = [];
    spare.append("q");
    var h = Box { s: a[..] };
    var bx = Box { s: spare[..] };
    put(h.s, bx);
    other.clear();
    other.append("uvw");
    print(bx.s);
    var by = Box { s: spare[..] };
    take(h, by);
    other.clear();
    other.append("rst");
    print(by.s);
    var bz = Box { s: spare[..] };
    let r = &h;
    bz = r;
    other.clear();
    print(bz.s);
    print(other.len);
}
