// error: cannot clear bs: global GS may hold a reference into it
// A store through a slice parameter viewing a global array.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][4] = ["", "", "", ""];
fn reload() { bs.clear(); }
fn put(xs: const u8[:][:], s: const u8[:]) { xs[0] = s; }
fn later() { put(GS[..], bs[0].name[..]); }
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
