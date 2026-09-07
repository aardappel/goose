// to_bytes / from_bytes (docs/design/serialization.md): an array of
// self-relative structure written out as bytes, and verified back in.
//
// The point of the verifier is that a *hostile* image can never produce a
// reference the checker did not prove valid, so most of this file is about
// images that are wrong: truncated, extended, retagged, relinked. The last
// section mutates a good image every way a short loop can and walks whatever
// survives, asserting through `index_of` that every link it followed really
// does land on an element of the array it came from.

// --- fixed elements ---------------------------------------------------------

struct Tree { v: i32, l: Tree&<u32>?, r: Tree&<u32>? }

fn tsum(a: Tree[>..]&) -> i64 {
    var acc = 0;
    for &n in a {
        acc = acc * 7 + n.v;
        let l = n.l;
        if l { acc += l.v; }
        let r = n.r;
        if r { acc += r.v; }
    }
    return acc;
}

fn build(out: Tree[>..]&) {
    let a: Tree& = out.push(Tree { v: 11 });
    let b: Tree& = out.push(Tree { v: 22 });
    let c: Tree& = out.push(Tree { v: 33, l: a, r: b });
    out.push(Tree { v: 44, l: c });
}

fn fixed_elements() {
    var pool: Tree[>..] = [];
    build(pool);
    var img: u8[>..] = pool.to_bytes();
    print(pool.len, " nodes, ", img.len, " bytes");     // 4 nodes, 48 bytes

    var back, ok = from_bytes<Tree[>..]>(img);
    print(ok, " ", back.len, " ", tsum(back) == tsum(pool));   // true 4 true

    // The result is an ordinary array rooted at its own variable: it grows,
    // and a relative store into it works like any other (§3.9).
    let n: Tree& = back.push(Tree { v: 55, l: back[0] });
    let nl = n.l;
    guard nl else { print("unreachable"); return; }
    print(back.len, " ", nl.v);                         // 5 11

    // A link that leaves the image: the offset field of the last node.
    var wild: u8[>..] = pool.to_bytes();
    wild[40] = 200;
    var w, wok = from_bytes<Tree[>..]>(wild);
    print("wild ", wok, " ", w.len);                    // wild false 0

    // A link that lands inside an element rather than on its start.
    var mid: u8[>..] = pool.to_bytes();
    mid[40] = mid[40] + 1;
    var m, mok = from_bytes<Tree[>..]>(mid);
    print("mid ", mok);                                 // mid false

    // A byte length that is not a whole number of elements.
    var cut: u8[>..] = pool.to_bytes();
    cut.pop();
    var c2, cok = from_bytes<Tree[>..]>(cut);
    print("cut ", cok);                                 // cut false

    // The empty image is a valid empty array.
    var none: u8[>..] = [];
    var e, eok = from_bytes<Tree[>..]>(none);
    print("empty ", eok, " ", e.len);                   // empty true 0
}

// --- self, and links that are never null ------------------------------------

struct Ring { v: u8, prev: Ring&<u16>, next: Ring&<u16> }

fn self_links() {
    var pool: Ring[>..] = [];
    pool.push(Ring { v: 7, prev: self, next: self });
    pool.push(Ring { v: 8, prev: self, next: self });
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Ring[>..]>(img);
    print("ring ", ok, " ", back.len, " ", back[0].next.v, " ",
          back[1].prev.v);                                      // ring true 2 7 8

    // A slice receiver writes out the elements it spans, nothing else.
    var tail: u8[>..] = to_bytes(pool[1..]);
    var t, tok = from_bytes<Ring[>..]>(tail);
    print("slice ", tok, " ", t.len, " ", t[0].v);                    // slice true 1 8
}

// --- a variant type as the element type -------------------------------------

enum Knot { Loop { n: u8, back: Knot.Loop&<u8> }, Plain { n: u8 } }

fn variant_elements() {
    var pool: Knot.Loop[>..] = [];
    let a: Knot.Loop& = pool.push(Knot.Loop { n: 1, back: self });
    pool.push(Knot.Loop { n: 3, back: a });
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Knot.Loop[>..]>(img);
    print("knot ", ok, " ", back.len, " ", back[1].back.n);   // knot true 2 1
}

// --- fixed-mode ADT elements: the tag is range-checked ----------------------

enum Shape { Circle { r: f32 }, Rect { w: f32, h: f32 }, Dot }

fn area(s: Shape&) -> f64 {
    match s {
        Circle c => 3.0 * c.r as f64 * c.r as f64,
        Rect r => r.w as f64 * r.h as f64,
        Dot => 0.0,
    }
}

fn adt_elements() {
    var pool: Shape[>..] = [];
    pool.push(Shape.Circle { r: 1.0 });
    pool.push(Shape.Rect { w: 2.0, h: 3.0 });
    pool.push(Shape.Dot {});
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Shape[>..]>(img);
    var sum = 0.0;
    for &s in back { sum += area(s); }
    print("shape ", ok, " ", back.len, " ", sum);       // shape true 3 9

    img[0] = 9;                                         // a tag out of range
    var b2, ok2 = from_bytes<Shape[>..]>(img);
    print("badtag ", ok2, " ", b2.len);                 // badtag false 0
}

// --- limited arrays inside an element ---------------------------------------
// Slots past the length were never written (§5.3), so the walk checks the
// links in the live ones only.

struct Cell { k: u16, kids: (Cell&<u16>)[..3] }

fn limited_fields() {
    var pool: Cell[>..] = [];
    let a: Cell& = pool.push(Cell { k: 1, kids: [] });
    let b: Cell& = pool.push(Cell { k: 2, kids: [a] });
    pool.push(Cell { k: 3, kids: [a, b] });
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Cell[>..]>(img);
    let r = &back[2];
    print("cell ", ok, " ", back.len, " ", r.kids.len, " ",
          r.kids[0].k, " ", r.kids[1].k);                 // cell true 3 2 1 2

    // A stored length above the capacity is not a value of the type.
    img[2] = 9;
    var b2, ok2 = from_bytes<Cell[>..]>(img);
    print("overlen ", ok2);                             // overlen false
}

// --- variable-size elements: a pool of variable-mode ADTs -------------------

enum Sexp {
    Sym  { name: u8[varint] },
    Pair { head: Sexp..&<varint>, tail: Sexp..&<varint>?, extra: (i16)[varint] },
}

fn sdigest(a: Sexp..[>..]&) -> i64 {
    var acc = 0;
    for s in a {
        match s {
            Sym &y => { for &c in y.name { acc = acc * 31 + c as i64; } },
            Pair &p => {
                acc = acc * 31 + p.head.stag();
                let t = p.tail;
                if t { acc = acc * 31 + t.stag(); }
                for &x in p.extra { acc = acc * 31 + x as i64; }
            }
        }
    }
    return acc;
}

fn stag(s: Sexp..&) -> i64 { match s { Sym &_ => 1, Pair &_ => 2 } }

fn build_sexp(out: Sexp..[>..]&) {
    let a: Sexp..& = out.push(Sexp.Sym { name: "alpha" });
    let b: Sexp..& = out.push(Sexp.Sym { name: "b" });
    let p: Sexp..& = out.push(Sexp.Pair { head: a, tail: b, extra: [1, -2, 3] });
    out.push(Sexp.Pair { head: p, extra: [] });
}

fn variable_elements() {
    var pool: Sexp..[>..] = [];
    build_sexp(pool);
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Sexp..[>..]>(img);
    print("sexp ", ok, " ", back.len, " ", img.len, " ",
          sdigest(back) == sdigest(pool));            // sexp true 4 24 true

    // One byte past the end: the framing walk no longer ends at the end.
    var over: u8[>..] = pool.to_bytes();
    over.push(0);
    var o, ook = from_bytes<Sexp..[>..]>(over);
    print("over ", ook);                                // over false

    // A varint length that claims more bytes than the image holds.
    var big: u8[>..] = pool.to_bytes();
    big[1] = 100;
    var g, gok = from_bytes<Sexp..[>..]>(big);
    print("biglen ", gok);                              // biglen false
}

// --- a reference typed at one variant of a variable-mode ADT ----------------
// Such a reference addresses the payload, so the verifier checks the element
// start behind it and the tag there.

enum Item {
    Word { text: u8[varint] },
    Ref  { to: Item.Word&<u16>? },
}

fn variant_pointees() {
    var pool: Item..[>..] = [];
    let w: Item..& = pool.push(Item.Word { text: "hi" });
    match w {
        Word &x => { pool.push(Item.Ref { to: x }); },
        Ref &_ => {}
    }
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Item..[>..]>(img);
    var n = 0;
    for e in back {
        match e {
            Word &x => { n += x.text.len; },
            Ref &r => { let t = r.to; if t { n += 10 * t.text.len; } }
        }
    }
    print("item ", ok, " ", back.len, " ", n);          // item true 2 22

    // Retagging the target makes the link point at a Ref, not a Word.
    img[0] = 1;
    var b2, ok2 = from_bytes<Item..[>..]>(img);
    print("retag ", ok2);                               // retag false
}

// --- nested arrays, alignment pads, and every receiver kind -----------------

struct Rec {
    id: u32,
    tags: u8[],                 // a u32 length prefix
    slots: i32[..],             // a runtime capacity: [cap][len][cap slots]
    link: Rec&<u32>?,
}

// A bare `pad` aligns the next field, so the field offsets are not the sum
// of the sizes before them and the walk has to read them off the layout.
struct Pad { a: u8, pad, b: u32, l: Pad&<u16>? }

fn recsum(a: Rec[>..]&) -> i64 {
    var n = 0;
    for r in a {
        n = n * 7 + r.id as i64 + r.tags.len + r.slots.len;
        let l = r.link;
        if l { n += l.id as i64; }
    }
    return n;
}

fn nested() {
    var pool: Rec[>..] = [];
    let a: Rec& = pool.push(Rec { id: 1, tags: "ab", slots: [1, 2, 3] });
    pool.push(Rec { id: 2, tags: "", slots: [], link: a });
    var img: u8[>..] = pool.to_bytes();
    var back, ok = from_bytes<Rec[>..]>(img);
    print("rec ", ok, " ", back.len, " ", img.len, " ",
          recsum(back) == recsum(pool));            // rec true 2 54 true

    // A stored capacity that would run past the end of the image.
    img[10] = 200;
    var b2, ok2 = from_bytes<Rec[>..]>(img);
    print("badcap ", ok2);                          // badcap false

    var pads: Pad[>..] = [];
    let pa: Pad& = pads.push(Pad { a: 1, b: 100 });
    pads.push(Pad { a: 2, b: 200, l: pa });
    var pimg: u8[>..] = pads.to_bytes();
    var pback, pok = from_bytes<Pad[>..]>(pimg);
    let pl = pback[1].l;
    guard pl else { print("unreachable"); return; }
    print("pad ", pok, " ", pback.len, " ", pimg.len, " ", pl.b);
                                                    // pad true 2 20 100
}

fn receivers() {
    // Every array kind, and a slice: the element region and nothing else, so
    // no length or capacity metadata reaches the image.
    let f: i16[4] = [1, 2, 3, 4];
    let v: i16[] = [5, 6, 7];
    var lim: i16[..8] = [8, 9];
    var gs: i16[>..<] = [10];
    print("recv ", f.to_bytes().len, " ", v.to_bytes().len, " ",
          lim.to_bytes().len, " ", gs.to_bytes().len);   // recv 8 6 4 2
    var back, ok = from_bytes<i16[>..]>(f.to_bytes());
    print("ints ", ok, " ", back.len, " ", back[3]);     // ints true 4 4
}

// --- destinations -----------------------------------------------------------
// The image is an ordinary resizable result (§7.3), so it lands wherever one
// does: a value slot with a length prefix, a field, a temporary, a return.

struct Saved { name: u8[], blob: u8[] }

fn image_of(pool: Tree[>..]&) -> u8[>..] { return pool.to_bytes(); }

fn nodes_in(b: u8[:]) -> i64 {
    var a, ok = from_bytes<Tree[>..]>(b);
    if !ok { return -1; }
    return a.len;
}

fn destinations() {
    var pool: Tree[>..] = [];
    build(pool);
    let flat: u8[] = pool.to_bytes();
    let s = Saved { name: "tree", blob: pool.to_bytes() };
    print("dest ", flat.len, " ", s.blob.len, " ", nodes_in(pool.to_bytes()), " ",
          image_of(pool).len, " ", nodes_in(s.blob));   // dest 48 48 4 48 4
}

// --- mutations --------------------------------------------------------------

var rng: u64 = 0x243f6a8885a308d3;

fn nextr() -> i64 {
    rng = rng * 6364136223846793005 + 1442695040888963407;
    return ((rng >> 33) & 0x7fffffff) as i64;
}

// Every link the walk follows, turned back into an element index: a link that
// escaped the array would be an index outside it, which is exactly what the
// verifier is there to make impossible.
fn checked_tsum(a: Tree[>..]&) -> i64 {
    var acc = 0;
    for &n in a {
        acc = acc * 7 + n.v;
        let l = n.l;
        if l { let i = a.index_of(l); assert(i >= 0 && i < a.len); acc += l.v; }
        let r = n.r;
        if r { let i = a.index_of(r); assert(i >= 0 && i < a.len); acc += r.v; }
    }
    return acc;
}

fn checked_cells(a: Cell[>..]&) -> i64 {
    var acc = 0;
    for &c in a {
        acc = acc * 7 + c.k as i64;
        for i in c.kids.len {
            let k: Cell& = c.kids[i];
            let j = a.index_of(k);
            assert(j >= 0 && j < a.len);
            acc += k.k as i64;
        }
    }
    return acc;
}

fn mutate(img: u8[>..]&, kind: i64) {
    guard img.len > 0 else { return; }
    if kind == 0 {
        let i = nextr() % img.len;
        img[i] = img[i] ^ (1 << (nextr() % 8)) as u8;
    } else if kind == 1 {
        let i = nextr() % img.len;
        img[i] = (nextr() % 256) as u8;
    } else if kind == 2 {
        let k = nextr() % img.len;
        for i in k { img.pop(); }
    } else {
        let k = 1 + nextr() % 4;
        for i in k { img.push((nextr() % 256) as u8); }
    }
}

fn mutations() {
    var trees: Tree[>..] = [];
    build(trees);
    var sexp: Sexp..[>..] = [];
    build_sexp(sexp);
    var cells: Cell[>..] = [];
    let ca: Cell& = cells.push(Cell { k: 1, kids: [] });
    let cb: Cell& = cells.push(Cell { k: 2, kids: [ca] });
    cells.push(Cell { k: 3, kids: [ca, cb] });

    var taken = 0;
    var digest = 0;
    for round in 900 {
        let kind = round % 4;
        var i1: u8[>..] = trees.to_bytes();
        mutate(i1, kind);
        var r1, o1 = from_bytes<Tree[>..]>(i1);
        if o1 { taken++; digest = digest * 3 + checked_tsum(r1); }

        var i2: u8[>..] = sexp.to_bytes();
        mutate(i2, kind);
        var r2, o2 = from_bytes<Sexp..[>..]>(i2);
        if o2 { taken++; digest = digest * 3 + sdigest(r2); }

        var i3: u8[>..] = cells.to_bytes();
        mutate(i3, kind);
        var r3, o3 = from_bytes<Cell[>..]>(i3);
        if o3 { taken++; digest = digest * 3 + checked_cells(r3); }
    }
    print("mutations ", taken > 0, " ", taken < 2700, " ", digest != 0);
                                                    // mutations true true true
}

fn main() {
    fixed_elements();
    self_links();
    variant_elements();
    adt_elements();
    limited_fields();
    nested();
    receivers();
    variable_elements();
    variant_pointees();
    destinations();
    mutations();
}
