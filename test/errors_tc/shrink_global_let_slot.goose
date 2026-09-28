// error: cannot clear bs: global P may hold a reference into it
// P is a `let`, but its type is not const, so a reference to its slot may
// write it: aim, checked after reload, points P into bs, and reload's second
// clear leaves it viewing freed elements (§5.1, §9.5).
struct B { name: u8[..8] }
var arr: u8[4] = [1, 2, 3, 4];
let P: u8[:] = arr[..];
var bs: B[>..] = [];
fn reload() { bs.clear(); }
fn aim(s: u8[:]&) { s = bs[0].name[..]; }
fn main() {
    bs.push(B { name: "a" });
    reload();
    aim(P);
    reload();
    bs.push(B { name: "zz" });
    print(P[0]);
}
