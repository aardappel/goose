// error: cannot clear bs: global G may hold a reference into it
// put copies an element of what the slice in GHB.hs views, GA: GHB's stores
// say where its slices point, not what those elements hold (§3.5, §5.1).
struct B { name: u8[..8] }
struct H { s: const u8[:] }
struct HB { hs: H[:] }
var G = H { s: "" };
var bs: B[>..] = [];
var GA: H[1] = [H { s: "" }];
var GHB = HB { hs: GA[0..0] };
fn reload() { bs.clear(); }
fn put(p: H[:]&, b: H&) { b = p[0]; }
fn later() {
    GA[0].s = bs[0].name[..];
    GHB.hs = GA[..];
    put(GHB.hs, G);
}
fn main() {
    bs.push(B { name: "a" });
    reload();
    bs.push(B { name: "a" });
    later();
    reload();
    bs.push(B { name: "zz" });
    print(G.s);
}
