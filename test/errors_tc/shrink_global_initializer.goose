// error: cannot clear bs: global G may hold a reference into it
// A global initializer's view, checked before any function.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: bs[0].name[..] };
fn reload() { bs.clear(); }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
