// error: cannot clear bs: global G may hold a reference into it
// A slice read back out of a local holder views what the stores into that
// holder put there: here a view of bs's element (§5.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn later() { var h = H { s: "" }; h.s = bs[0].name[..]; G.s = h.s; }
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
