// error: cannot clear bs: global G may hold a reference into it
// A store through a reference read back out of a global, which may be
// any global that can hold an H.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
struct HR { h: H& }
var G = H { s: "" };
var GR = HR { h: G };
fn reload() { bs.clear(); }
fn later() { let r .= GR.h; r.s = bs[0].name[..]; }
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
