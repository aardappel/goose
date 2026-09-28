// error: cannot clear bs: global P may hold a reference into it
// A var slice global rebound to a view of bs by a function checked after
// reload.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var P: const u8[:] = "x";
fn reload() { bs.clear(); }
fn later() { P = bs[0].name[..]; }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(P);
}
