// error: cannot clear bs while hh is still used: a reference into it was stored there
// The block outer hands to each stores v into the H outer's parameter h
// refers to. each's summary records the store against a class of outer's,
// which outer's own summary carries to main's call: a store of a view of
// bs into hh (§5.1, §7.6).
struct H { s: const u8[:] }
struct B { name: u8[..8] }
fn each<F>(n: i64) { for i in n { F(i); } }
fn outer(h: H&, v: const u8[:]) {
    each(1) { h.s = v; }
}
fn main() {
    var bs: B[>..] = [];
    bs.push(B { name: "a" });
    var hh = H { s: "" };
    outer(hh, bs[0].name[..]);
    bs.clear();
    bs.push(B { name: "zz" });
    print(hh.s);
}
