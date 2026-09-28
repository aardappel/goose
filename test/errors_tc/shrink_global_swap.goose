// error: cannot clear bs: global G2 may hold a reference into it
// A swap through references moves what G2 held into G; G2 held the view
// first, which is what the error names (its store record is flow-insensitive).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
var G2 = H { s: "" };
fn reload() { bs.clear(); }
fn swap_h(a: H&, b: H&) { let t = a; a = b; b = t; }
fn later() { G2.s = bs[0].name[..]; swap_h(G, G2); }
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
