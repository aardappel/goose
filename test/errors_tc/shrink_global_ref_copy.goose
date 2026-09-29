// error: cannot clear bs: global G may hold a reference into it
// G takes a copy of what G2 holds, loaded through a reference to it, not a
// reference into G2: G2's stores say it holds a view of bs (§3.5, §5.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var G = H { s: "" };
var bs: B[>..] = [];
var G2 = H { s: "" };
fn reload() { bs.clear(); }
fn later() {
    G2.s = bs[0].name[..];
    let r = &G2;
    G = r;
}
fn main() {
    bs.push(B { name: "a" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
