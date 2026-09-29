// error: cannot clear bs: global G may hold a reference into it, stored at
// put's first call stores s while it is still a literal; the second finds s
// viewing an element of bs, so it is checked again, and the store record
// shows that view stored into G (§5.1, §7.5).
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
