// error: cannot clear hs: global G may hold a reference into it
// keep reads a view out of what its parameter refers to: at the call,
// hs[0], whose s views hs's own element. The storage behind keep's
// parameter is what each call passed, and what G is given is what that
// storage may hold (§5.1).
struct H { s: const u8[:], name: u8[..8] }
var hs: H[>..] = [];
var G = H { s: "", name: "" };
fn reload() { hs.clear(); }
fn keep(r: H&) { G.s = r.s; }
fn later() {
    hs[0].s = hs[0].name[..];
    keep(hs[0]);
}
fn main() {
    hs.push(H { s: "", name: "a" });
    reload();
    hs.push(H { s: "", name: "a" });
    later();
    reload();
    hs.push(H { s: "", name: "zz" });
    print(G.s);
}
