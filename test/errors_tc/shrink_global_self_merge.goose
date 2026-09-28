// error: cannot clear hs: global G may hold a reference into it
// The view is read out of a copy of hs or of H2, chosen at run time; hs's
// elements view hs's own. What the copy holds is known only as some
// global's contents, which may be hs's (§5.1).
struct H { s: const u8[:], name: u8[..8] }
var hs: H[>..] = [];
var H2: H[>..] = [];
var G = H { s: "", name: "" };
fn reload() { hs.clear(); }
fn later(c: bool) {
    hs[0].s = hs[0].name[..];
    H2.push(H { s: "", name: "b" });
    G.s = (if c { copy(hs) } else { copy(H2) })[0].s;
}
fn main() {
    hs.push(H { s: "", name: "a" });
    reload();
    hs.push(H { s: "", name: "a" });
    later(true);
    reload();
    hs.push(H { s: "", name: "zz" });
    print(G.s);
}
