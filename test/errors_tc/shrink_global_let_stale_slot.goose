// error: cannot clear bs: global P may hold a reference into it
// error: its initializer's roots may not say all it points at, since a slice is written through a reference
// pick returns s after aim has written a view of bs's element into it
// through a reference, which leaves s's roots saying arr: P, bound for good
// to pick's result, views bs, so the roots its initializer was given cannot
// settle that it does not (§5.1).
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
