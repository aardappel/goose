// error: cannot clear bs: global GS may hold a reference into it
// A store into an element of a global array of slices.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][4] = ["", "", "", ""];
fn reload() { bs.clear(); }
fn later() { GS[1] = bs[0].name[..]; }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(GS[1]);
}
