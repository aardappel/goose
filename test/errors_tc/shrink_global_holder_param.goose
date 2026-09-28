// error: cannot clear bs: global G may hold a reference into it
// A by-value holder parameter copied into the global; the second call
// passes a literal holding the view.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reload() { bs.clear(); }
fn keep(h: H) { G = h; }
fn later() { keep(H { s: cs[0].name[..] }); keep(H { s: bs[0].name[..] }); }
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
