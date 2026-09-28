// error: cannot clear bs: global G may hold a reference into it
// A nested function's store through its parent's parameter, which the
// parent's call binds to the global.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn outer(h: H&, v: const u8[:]) {
    fn inner() { h.s = v; }
    inner();
}
fn later() { outer(G, bs[0].name[..]); }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
