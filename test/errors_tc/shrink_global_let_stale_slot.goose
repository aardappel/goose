// error: cannot clear bs: global P may hold a reference into it, stored at
// aim writes a view of bs's element into pick's s through a reference to
// its slot, which assigns s (§3.8): P, bound for good to pick's result,
// views bs (§5.1).
struct B { name: u8[..8] }
var arr: u8[4] = [1, 2, 3, 4];
var bs: B[>..] = [B { name: "a" }];
fn aim(r: u8[:]&, v: u8[:]) { r = v; }
fn pick() -> const u8[:] {
    var s: u8[:] = arr[..];
    aim(s, bs[0].name[..]);
    return s;
}
let P: const u8[:] = pick();
fn main() {
    bs.clear();
    bs.push(B { name: "zz" });
    print(P);
}
