// error: cannot clear bs: global G may hold a reference into it
// keep's default is a view of bs's element, which keep stores into G: the
// call leaving it out stores that view as one passing it would (§5.1,
// §7.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var G = H { s: "" };
fn keep(v: const u8[:] = bs[0].name[..]) { G.s = v; }
fn reload() { bs.clear(); }
fn main() {
    bs.push(B { name: "a" });
    reload();
    bs.push(B { name: "a" });
    keep();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
