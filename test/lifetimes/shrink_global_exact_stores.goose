// A global holds a reference only where a store or a binding put one, and
// once every function has been checked all of them are on record (§5.1):
// clearing bs, or the builder out, needs no care for globals that view cs,
// static data, or each other, whichever functions stored them there, and
// through whichever calls, specializations and function values.
struct B { name: u8[..8] }
struct H { s: const u8[:] }
struct W { h: H& }
let PATH = "x.save";
var P2 = "y.save";
let V: const u8[:] = "v";
var out: u8[>..] = [];
var bs: B[>..] = [];
var cs: B[>..] = [];
var G = H { s: "" };
var G2 = H { s: "" };
var GS: const u8[:][>..] = [];
var GS2: const u8[:][>..] = [];
var GH: H[>..] = [];
var R: B? = null;

fn emit() {
    out.clear();
    out.append(PATH);
}
fn reload() { bs.clear(); }
fn put(s: const u8[:]) { G.s = s; }
fn keep(h: H) { G2 = h; }
fn put_through(w: W, s: const u8[:]) { w.h.s = s; }
fn each_b<F>(xs: B[:]) { for i in xs.len { F(xs[i].name[..]); } }

// Checked after emit and reload: everything it stores views cs, or is a
// literal.
fn later() {
    P2 = "z.save";
    put(cs[0].name[..]);
    put("lit");
    keep(H { s: cs[0].name[..] });
    GS.push("a");
    GS.push(cs[0].name[..]);
    GH.push(H { s: cs[0].name[..] });
    R .= cs[0];
    put_through(W { h: G2 }, cs[0].name[..]);
    each_b(cs[..]) { n => GS.push(n); }
    fn nested(s: const u8[:]) { GH.push(H { s: s }); }
    nested(cs[0].name[..]);
    G2 = G;
    // Read back out of a global array, which holds views of cs only.
    GS2.push(cs[0].name[..]);
    P2 = GS2[0];
    put(GS2[0]);
}

fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    emit();
    reload();
    later();
    reload();
    bs.push(B { name: "b" });
    emit();
    print(out, " ", P2, " ", V, " ", G.s, " ", G2.s, " ", GS.len, " ", GS[2], " ", GH.len,
          " ", GH[1].s, " ", bs[0].name);
    if R { print(R.name); }
}
