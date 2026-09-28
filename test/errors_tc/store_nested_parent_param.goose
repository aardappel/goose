// error: cannot clear bs while hh is still used: a reference into it was stored there
// inner stores v into the H its parent's parameter h refers to: at outer's
// call that is a store of a view of bs into hh, which outer's summary
// carries for inner (§5.1, §7.5). hh.s would read the cleared element.
struct H { s: const u8[:] }
struct B { name: u8[..8] }
fn outer(h: H&, v: const u8[:]) {
    fn inner() { h.s = v; }
    inner();
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
