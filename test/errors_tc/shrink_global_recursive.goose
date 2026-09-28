// error: cannot clear bs: global G may hold a reference into it
// A store deep in a recursive function, of what its entry call passed.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
recursive fn put(s: const u8[:], n: i64) {
    if n > 0 { put(s, n - 1); } else { G.s = s; }
}
fn later() { put(bs[0].name[..], 2); }
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
