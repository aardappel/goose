// error: cannot clear bs: global P may hold a reference into it, stored at
// aim writes the view into P's slot through a reference bound to it, which
// assigns P (§3.8): the record says P may view bs (§5.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var arr: u8[4] = [1, 2, 3, 4];
var P: u8[:] = arr[..];
fn reload() { bs.clear(); }
fn aim(r: u8[:]&, v: u8[:]) { r = v; }
fn later() { aim(P, bs[0].name[..]); }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(P.len);
}
