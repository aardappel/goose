// error: cannot clear bs: global G may hold a reference into it
// A store through a reference that may be the global or a local: it is a
// store into both (ShrinkTargets).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn later(c: bool) {
    var loc = H { s: "" };
    let r: H& = if c { G } else { loc };
    r.s = bs[0].name[..];
    print(loc.s);
}
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later(true);
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
