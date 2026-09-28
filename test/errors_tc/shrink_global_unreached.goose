// error: cannot clear bs: global G may hold a reference into it
// A store in a function no call reaches: it counts, as every error such a
// function has does.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn never() { G.s = bs[0].name[..]; }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
