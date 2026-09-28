// error: cannot clear bs: global R may hold a reference into it
// A var reference global rebound to an element of bs by a function checked
// after reload.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var R: B? = null;
fn reload() { bs.clear(); }
fn later() { R .= bs[0]; }
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    if R { print(R.name); }
}
