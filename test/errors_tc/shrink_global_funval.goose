// error: cannot clear bs: global G may hold a reference into it
// A function value's body storing what the function it is handed passes
// it: each_b's class stands for both arrays.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn each_b<F>(xs: B[:]) { for i in xs.len { F(xs[i].name[..]); } }
fn later() {
    each_b(cs[..]) { n => G.s = n; }
    each_b(bs[..]) { n => G.s = n; }
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
