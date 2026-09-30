// error: cannot clear out while h is in scope: a reference into it is stored there
// error: , which the next iteration reaches
// The block puts what once hands it, a view of out, into h after the clear,
// so the next iteration's clear would invalidate it: once's call stores
// into h what it passed, which the loop's next pass sees (§5.1).
struct H { s: const u8[:] }
fn put(h: H&, s: const u8[:]) { h.s = s; }
fn once<F>(s: const u8[:]) { F(s[0..1]); }
fn main() {
    var out: u8[>..] = [];
    out.append("abc");
    var h = H { s: "k" };
    for i in 0..2 {
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(h.s);
        once(out[..]) { s => put(h, s); };
    }
}
