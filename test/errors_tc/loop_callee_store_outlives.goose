// error: storing a reference rooted at local, which does not outlive the destination
// put gives h a view of local after h is assigned to out, so the next
// iteration puts a view of local into the caller's storage, which local does
// not outlive: the loop's next pass sees the store whichever function makes
// it (§9.2).
struct H { s: const u8[:] }
fn put(h: H&, s: const u8[:]) { h.s = s; }
fn f(out: H&) {
    var local: u8[>..] = [];
    local.append("abc");
    var h = H { s: "k" };
    for i in 0..2 {
        out = h;
        put(h, local[..]);
    }
}
fn main() {
    var o = H { s: "" };
    f(o);
    print(o.s);
}
