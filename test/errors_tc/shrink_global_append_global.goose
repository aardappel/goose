// error: cannot clear bs: global GS may hold a reference into it
// An append of another global's elements, which that global no longer
// holds after its own clear.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][>..] = [];
var GS2: const u8[:][>..] = [];
fn reload() { bs.clear(); }
fn later() {
    GS2.push(bs[0].name[..]);
    GS.append(GS2[..]);
    GS2.clear();
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
