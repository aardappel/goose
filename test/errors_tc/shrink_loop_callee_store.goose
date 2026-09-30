// error: cannot clear out while h is in scope: a reference into it is stored there
// error: , which the next iteration reaches
// put stores a view of out into h after the clear, so the next iteration's
// clear would invalidate it: the loop's next pass sees the store whichever
// function makes it (§5.1).
struct H { s: const u8[:] }
fn put(h: H&, s: const u8[:]) { h.s = s; }
fn main() {
    var out: u8[>..] = [];
    out.append("abc");
    var h = H { s: "k" };
    for i in 0..2 {
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(h.s);
        put(h, out[..]);
    }
}
