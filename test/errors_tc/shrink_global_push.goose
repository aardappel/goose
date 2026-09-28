// error: cannot clear bs: global GS may hold a reference into it
// A push of a view onto a global array of slices.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][>..] = [];
fn reload() { bs.clear(); }
fn later() { GS.push(bs[0].name[..]); }
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
