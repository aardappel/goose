// error: cannot clear bs: global G may hold a reference into it
// error: the roots on record may not say all a value points at, since s
// put's body is checked once, at its first call, when s is still a literal;
// the second call stores the view s holds by then, which the store record
// does not show: every global that can hold a view into bs counts.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn later() {
    var s: const u8[:] = "x";
    fn put() { G.s = s; }
    put();
    s = bs[0].name[..];
    put();
}
fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
