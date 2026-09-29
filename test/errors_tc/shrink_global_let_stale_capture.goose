// error: cannot clear bs: global P may hold a reference into it, stored at
// keep's first call finds s viewing arr; the second finds it viewing an
// element of bs as well, so it is checked again, and the view it copies into
// out may be of bs's element. pick returns out, and P, bound for good to
// pick's result, may view bs (§5.1, §7.5).
struct B { name: u8[..8] }
var arr: u8[4] = [1, 2, 3, 4];
var bs: B[>..] = [B { name: "a" }];
fn pick() -> const u8[:] {
    var s: const u8[:] = arr[..];
    var out: const u8[:] = arr[..];
    fn keep() { out = s; }
    keep();
    s = bs[0].name[..];
    keep();
    return out;
}
let P: const u8[:] = pick();
fn main() {
    bs.clear();
    bs.push(B { name: "zz" });
    print(P);
}
