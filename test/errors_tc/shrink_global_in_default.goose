// error: cannot pop bs: global G may hold a reference into it
// The default count's call takes pops bs where the call stands, and later
// stores a view of bs's element into G: the pop is judged against the
// other globals once the program is checked, as one written at the call
// would be (§5.1, §7.1).
struct B { name: u8[..8], v: i64 }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var G = H { s: "" };
fn count(n: i64 = bs.pop().v) -> i64 { n }
fn later() { G.s = bs[0].name[..]; }
fn main() {
    bs.push(B { name: "a", v: 1 });
    bs.push(B { name: "b", v: 2 });
    print(count());
    later();
    print(count());
    bs.push(B { name: "zz", v: 3 });
    print(G.s);
}
