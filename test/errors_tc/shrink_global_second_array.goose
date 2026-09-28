// error: cannot clear bs: global G may hold a reference into it
// Each global array shrunk is judged on its own: cs's clear, checked
// first, frees nothing G views, and bs's does (§5.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
fn reset_cs() { cs.clear(); }
fn reload() { bs.clear(); }
fn later() { G.s = bs[0].name[..]; }
fn main() {
    reset_cs();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
