// error: cannot clear bs: global G may hold a reference into it
// A store through the reference a by-value holder parameter holds, which
// the call points at the global.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
struct W { h: H& }
var G = H { s: "" };
fn reload() { bs.clear(); }
fn put(w: W, s: const u8[:]) { w.h.s = s; }
fn later() { put(W { h: G }, bs[0].name[..]); }
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
