// error: cannot clear bs: global G may hold a reference into it
// A copy of another global's contents, which that global no longer holds
// itself.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
var G2 = H { s: "" };
fn reload() { bs.clear(); }
fn later() { G2.s = bs[0].name[..]; G = G2; G2.s = ""; }
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
