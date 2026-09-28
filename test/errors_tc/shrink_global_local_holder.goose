// error: cannot clear bs: global G may hold a reference into it
// A local holder built from the parameter is copied into the global; the
// second call passes the view.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn put(s: const u8[:]) { var h = H { s: s }; G = h; }
fn later() { put(cs[0].name[..]); put(bs[0].name[..]); }
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
