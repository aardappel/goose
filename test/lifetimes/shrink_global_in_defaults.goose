// Defaults that shrink a global array, directly or through a call, beside
// a global a default gives a view of another array only, and a let save
// path: judged against the other globals as shrinks written at the call are
// (§5.1, §7.1).
struct B { name: u8[..8], v: i64 }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
let PATH = "x.save";
fn drop_last() -> i64 { bs.pop(); bs.len }
fn count(n: i64 = drop_last()) -> i64 { n }
fn take(n: i64 = bs.pop().v) -> i64 { n }
fn keep(v: const u8[:] = cs[0].name[..]) { G.s = v; }
fn main() {
    cs.push(B { name: "c", v: 9 });
    bs.push(B { name: "a", v: 1 });
    bs.push(B { name: "b", v: 2 });
    bs.push(B { name: "d", v: 4 });
    keep();
    print(take(), " ", count(), " ", count(bs.len), " ", G.s, " ", PATH);
}
