// error: cannot clear bs: global V may hold a reference into it
// V is a `let`, bound for good, but to a view of bs's elements, which the
// clear frees (§5.1).
struct B { name: u8[..8] }
var bs: B[>..] = [B { name: "a" }];
let V: const u8[:] = bs[0].name[..];
fn main() {
    bs.clear();
    print(V);
}
