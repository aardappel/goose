// error: storing a reference into p, which holds a grow-shrink array
// A parameter bound to an explicit `&s` stands for the slice s holds (§3.8),
// which may be the view of the grow-shrink G one branch of its binding gives
// (§9.2), although its root is `b`; `keep` may not store what it loads
// through the parameter (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }

fn keep(p: const u8[:]&, into: K[>..]&) { into.push(K { s: p }); }

fn main() {
    var b: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    var s: const u8[:] = if hs.len == 0 { G[..] } else { b[..] };
    keep(&s, hs);
    G.clear();
    G.append("pqr");
    print(hs[0].s);
}
