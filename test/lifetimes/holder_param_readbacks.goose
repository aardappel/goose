// A view read back out of a by-value parameter -- directly, out of a local
// copy, out of a matched payload or a `for` binder, or out of a local array
// filled from a parameter's elements -- is bounded by the parameter's class,
// so it is returned to callers whose storage outlives it; one of a pointee
// no literal supplies is not rooted at the callee's local copy either (§9.5).
struct W { s: const u8[:] }
struct N { xs: i64[:] }
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }

fn get(w: W) -> const u8[:] { w.s }
fn getcopy(w: W) -> const u8[:] { let w2 = w; w2.s }
fn getn(n: N) -> i64[:] { let n2 = n; n2.xs }
fn txt(e: E) -> const u8[:] { match e { Txt t => t.s, Nothing n => "-" } }
fn firstof(ws: W[:]) -> const u8[:] {
    for w in ws { if w.s.len > 3 { return w.s; } }
    ws[0].s
}
fn first(words: const u8[:][:]) -> const u8[:] {
    var parts: const u8[:][>..] = [];
    parts.push(words[0]);
    parts[0]
}

fn main() {
    var y: u8[>..] = [];
    for i in 5 { y.push('a' + i as u8); }
    let w = W { s: y[..] };
    var a: i64[>..] = [7, 8];
    var ws: const u8[:][>..] = [];
    ws.push(y[1..]);
    var hs: W[>..] = [];
    hs.push(W { s: y[3..] });
    hs.push(w);
    let s = get(w);
    let c = getcopy(w);
    let n = N { xs: a[..] };
    let xs = getn(n);
    xs[0] = 9;
    print(s, " ", c, " ", a[0], " ", firstof(hs), " ", first(ws));
    print(txt(E.Txt { s: y[2..] }), " ", txt(E.Nothing { k: 1 }), " ", get(W { s: "lit" }));
}
