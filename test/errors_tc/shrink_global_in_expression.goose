// error: cannot pop bs: global GS may hold a reference into it
// A grow-only shrink inside a larger expression is judged against the other
// globals once the program is checked, as one standing alone is (§5.1).
struct B { name: u8[..8] }
var bs: B[>..] = [];
var GS: const u8[:][>..] = [];
fn later() { GS.push(bs[0].name[..]); }
fn drop_last() { print("dropped ", bs.pop().name, " of ", bs.len + 1); }
fn main() {
    bs.push(B { name: "a" });
    bs.push(B { name: "b" });
    later();
    drop_last();
    bs.push(B { name: "zz" });
    print(GS[0]);
}
