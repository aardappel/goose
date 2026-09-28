// error: cannot clear ws: global G may hold a reference into it
// r may be W1 or ws[0], whose h.s views ws's own element; the copy of r.h
// into G is on record as a copy of what one of them holds, and what the
// other holds counts beside it. ws's view into its own elements is freed
// with them; G, holding a copy of it, is what the error names (§5.1).
struct H { s: const u8[:] }
struct W { h: H, name: u8[..8] }
var ws: W[>..] = [];
var W1 = W { h: H { s: "" }, name: "w" };
var G = H { s: "" };
fn reload() { ws.clear(); }
fn later(c: bool) {
    ws[0].h.s = ws[0].name[..];
    let r: W& = if c { W1 } else { ws[0] };
    G = r.h;
}
fn main() {
    ws.push(W { h: H { s: "" }, name: "a" });
    reload();
    ws.push(W { h: H { s: "" }, name: "a" });
    later(false);
    reload();
    ws.push(W { h: H { s: "" }, name: "zz" });
    print(G.s);
}
