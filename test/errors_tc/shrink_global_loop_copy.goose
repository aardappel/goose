// error: cannot clear bs: global G may hold a reference into it
// A copy of a local holder, which a later statement of the loop gives the
// view that the next iteration copies.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn later() {
    var h = H { s: "" };
    for i in 2 {
        G = h;
        h.s = bs[0].name[..];
    }
}
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
