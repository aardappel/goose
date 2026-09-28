// error: cannot clear bs: global P may hold a reference into it
// error: the roots on record may not say all a value points at, since a slice is written through a reference
// aim writes the view into P's slot through a reference bound to it, which
// the store record puts where P pointed: every global that can hold a view
// into bs counts.
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
