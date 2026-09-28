// error: cannot clear bs: global G may hold a reference into it
// The second call of one specialization passes the view: the store in
// put is rooted at its parameter's class, which stands for what every call
// passed, not only the first.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn put(s: const u8[:]) { G.s = s; }
fn later() { put(cs[0].name[..]); put(bs[0].name[..]); }
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
