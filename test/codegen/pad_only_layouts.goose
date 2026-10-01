// Structs and variants holding only pads take what the pads insert (§3.2),
// in C as in their layout. A pad with a size is that many bytes in both; a
// bare pad, which aligns only a field after it, or `pad 0` leaves none, and
// C, which has no empty structs, gives such a struct or variant one byte
// (C.2). A struct takes it in its layout too, but a variant takes no bytes
// behind its ADT's tag, so a fixed-mode ADT's C union has no member for it,
// and its variant type is never a field or an element (§3.4). Tags here is
// all tag: element 0's n would lose its top byte to element 1's tag, and
// `==` would miss it, if its C struct were bigger than its layout. A Gap
// rendered with an overload for a part (Num) renders each payload from a
// copy, which a payload without bytes has none to take.
struct Pads { pad 4 }
struct Bare { pad }
struct Held { p: Pads, b: Bare, n: i64 }
struct Num { n: i64 }
enum Gap { Hole { pad }, Zero { pad 0 }, Wide { pad 3 }, Fill { v: Num } }
enum Tags { A, B { pad } }
struct Tagged { t: Tags, n: i64 }

fn kind(h: Gap.Hole) -> i64 { 1 }
fn kind(z: Gap.Zero) -> i64 { 2 }
fn kind(w: Gap.Wide) -> i64 { 3 }
fn kind(f: Gap.Fill) -> i64 { f.v.n }

fn format(out: u8[>..]&, v: Num) { format(out, "#", v.n); }

fn main() {
    var hs: Held[>..] = [];
    hs.push(Held { p: Pads {}, b: Bare {}, n: 0x7f00000000000001 });
    hs.push(Held { p: Pads {}, b: Bare {}, n: 0x7e00000000000002 });
    print(hs[0].n, " ", hs[1].n, " ", hs[0] == hs[1], " ", hs[1] == hs[1]);
    let ps: Pads[2] = [Pads {}, Pads {}];
    print(ps, " ", ps[0] == ps[1], " ", hs[0].b);

    var ts: Tagged[>..] = [];
    ts.push(Tagged { t: Tags.B {}, n: 0x7f00000000000003 });
    ts.push(Tagged { t: Tags.A {}, n: 4 });
    ts.push(Tagged { t: Tags.B {}, n: 5 });
    let x = Tagged { t: Tags.B {}, n: 0x7f00000000000003 };
    let y = Tagged { t: Tags.B {}, n: 3 };
    print(ts[0].n, " ", ts[1].n, " ", ts[2].n, " ", ts[0].t, " ", ts[1].t);
    print(x == y, " ", ts[0] == x, " ", ts[1].t == ts[2].t);

    let h = Gap.Hole {};
    let w = Gap.Wide {};
    let gaps: Gap[4] = [h, Gap.Zero {}, w, Gap.Fill { v: Num { n: 9 } }];
    for g in gaps {
        match g {
            Hole hh => print(g, " ", kind(g), " ", hh),
            Zero zz => print(g, " ", kind(g), " ", zz),
            Wide ww => print(g, " ", kind(g), " ", ww),
            Fill f => print(g, " ", kind(g), " ", f.v.n),
        }
    }
    var vgs: Gap..[] = [gaps[0], gaps[1], gaps[2], gaps[3]];
    for g in vgs {
        match g {
            Hole &hh => print(hh),
            Zero &zz => print(zz),
            Wide &ww => print(ww),
            Fill &f => print(f.v.n),
        }
    }
    let z: Gap = Gap.Zero {};
    let wg: Gap = w;
    print(gaps[0] == gaps[1], " ", gaps[1] == z, " ", gaps[2] == wg, " ", vgs.len);
}
