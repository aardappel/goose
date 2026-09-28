// error: cannot clear bs: global P may hold a reference into it
// error: the roots on record may not say all a value points at, since s
// keep's body is checked once, at its first call, when s still views arr;
// its second call copies the view of bs's element s holds by then into out,
// and pick returns that. P, bound for good to pick's result, views bs,
// whatever the roots its initializer was given say (§5.1, §7.5).
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
