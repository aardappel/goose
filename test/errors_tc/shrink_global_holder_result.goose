// error: cannot clear bs: global G may hold a reference into it
// A holder a function builds from its argument; the second call's result
// holds the view.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn mk(s: const u8[:]) -> H { return H { s: s }; }
fn later() { G = mk(cs[0].name[..]); G = mk(bs[0].name[..]); }
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
