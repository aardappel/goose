// error: cannot clear bs: global GH may hold a reference into it
// A push of a holder variable onto a global array of holders.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GH: H[>..] = [];
fn reload() { bs.clear(); }
fn later() { let h = H { s: bs[0].name[..] }; GH.push(h); }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(GH[0].s);
}
