// error: cannot clear bs: global GS may hold a reference into it
// An append of a local array's views onto a global array.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][>..] = [];
fn reload() { bs.clear(); }
fn later() {
    var loc: const u8[:][>..] = [];
    loc.push(bs[0].name[..]);
    GS.append(loc);
}
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(GS[0]);
}
