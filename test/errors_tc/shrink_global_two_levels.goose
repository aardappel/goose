// error: cannot clear bs: global G may hold a reference into it
// The same through a call between.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn put(s: const u8[:]) { G.s = s; }
fn put2(s: const u8[:]) { put(s); }
fn later() { put2(cs[0].name[..]); put2(bs[0].name[..]); }
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
