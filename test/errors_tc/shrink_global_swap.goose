// error: cannot clear bs: global G may hold a reference into it
// A swap through references moves what G2 held, the view, into G. The round
// trip through t is judged by bound, not by what G2's stores say it holds:
// what G holds after the call is bounded, and may be a view of bs (§3.5).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
var G2 = H { s: "" };
fn reload() { bs.clear(); }
fn swap_h(a: H&, b: H&) { let t = a; a = b; b = t; }
fn later() { G2.s = bs[0].name[..]; swap_h(G, G2); }
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
