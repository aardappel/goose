// A frame object (C.2) holds its fixed fields as C members, and an `in pool`
// reference among them stores its offset from the pool's base, range-checked
// (§3.9), however the literal building it gets the reference: directly, from
// an if, a match, a block or a call (inlined at -O2), in a nested frame
// object or one behind a variable-size prefix, or inside a fixed struct or
// array field. An if's, a match's or a call's value stores the same way into
// the relative slots of fixed and variable-size literals, array literals,
// pushed and filled elements and a rebound field. The last store needs more
// than a u8 offset and aborts.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

struct W { r: Cell&<u32 in cells>?, items: i64[>..] }
struct Widths { a: u8, r8: Cell&<u8 in cells>, r16: Cell&<u16 in cells>?, b: u8,
                r64: Cell&<u64 in cells>?, items: i64[>..] }
struct Outer { r: Cell&<u16 in cells>, w: W }
struct Deeper { k: i64, o: Outer }
struct Tagged { tag: u8[], w: W }
struct Link { r: Cell&<u32 in cells>, n: i64 }
struct Linked { l: Link, ends: (Cell&<u32 in cells>?)[2], items: i64[>..] }
struct Small { r: Cell&<u8 in cells>?, items: i64[>..] }

reusable var nodes: Cell[>..] = [];
struct Holder { first: Cell&<u32 in nodes>, last: Cell&<u8 in nodes>?, items: i64[>..] }

struct Fixed { r: Cell&<u32 in cells>?, n: i64 }
struct Named { name: u8[], r: Cell&<u16 in cells>? }
struct Packed { name: u8[], r: Cell&<varint in cells>? }

var gw = W { r: null, items: [] };

fn pick(i: i64) -> Cell& { cells[i] }
fn pick_opt(i: i64) -> Cell&? {
    if i < 0 { return null; }
    return cells[i];
}
fn show(r: Cell&?) -> i64 {
    if r { return r.v; }
    return -1;
}
fn make(i: i64) -> W { W { r: cells[i], items: [i] } }
fn take(w: W) -> i64 { show(w.r) * 10 + w.items.len }
fn relink(w: W&, c: Cell&) { w.r .= c; }

fn main() {
    for i in 40 { cells.push(Cell { v: i }); }
    let c3: Cell& = cells[3];

    var w1 = Widths { a: 1, r8: c3, r16: cells[7], b: 2, r64: null, items: [5] };
    print(w1.a, " ", w1.r8.v, " ", show(w1.r16), " ", w1.b, " ", show(w1.r64), " ", w1.items);
    var w2 = Widths { a: 3, r8: cells[31], r16: pick_opt(-1), b: 4, r64: cells[39], items: [] };
    print(w2.a, " ", w2.r8.v, " ", show(w2.r16), " ", w2.b, " ", show(w2.r64));
    var w3 = Widths { a: 5, r8: c3, b: 6, items: [] };
    print(show(w3.r16), " ", show(w3.r64), " ", index_of(cells, w1.r8), " ",
          index_of(cells, w2.r8));

    for k in 3 {
        var a = W { r: if k == 1 { c3 } else { cells[k] }, items: [] };
        var b = W { r: match k { 0 => c3, 1 => null, _ => cells[9] }, items: [k] };
        var c = W { r: { let t: Cell& = cells[k + 10]; t }, items: [] };
        var d = W { r: pick(k + 20), items: [] };
        var e = W { r: pick_opt(k - 1), items: [] };
        var f = W { r: a.r, items: [] };
        print(show(a.r), " ", show(b.r), " ", show(c.r), " ", show(d.r), " ", show(e.r), " ",
              show(f.r));
    }

    var o = Outer { r: cells[4], w: W { r: cells[5], items: [1, 2] } };
    o.w.items.push(3);
    print(o.r.v, " ", show(o.w.r), " ", o.w.items);
    var deep = Deeper { k: 6, o: Outer { r: if o.r.v == 4 { cells[6] } else { c3 },
                                         w: W { r: pick(7), items: [] } } };
    print(deep.k, " ", deep.o.r.v, " ", show(deep.o.w.r));
    let lk = Link { r: cells[8], n: 1 };
    var l1 = Linked { l: Link { r: cells[9], n: 2 }, ends: [cells[10], null], items: [] };
    var l2 = Linked { l: lk, ends: [null, if lk.n == 1 { cells[11] } else { null }], items: [4] };
    print(l1.l.r.v, " ", l1.l.n, " ", show(l1.ends[0]), " ", show(l1.ends[1]), " ",
          l2.l.r.v, " ", l2.l.n, " ", show(l2.ends[0]), " ", show(l2.ends[1]));

    print(show(gw.r));
    gw = W { r: cells[12], items: [7, 8] };
    print(show(gw.r), " ", gw.items);
    var m = make(13);
    print(show(m.r), " ", m.items);
    print(take(W { r: cells[14], items: [1, 2] }), " ", take(make(15)), " ", take(copy(m)));
    relink(m, cells[16]);
    var kept = copy(m);
    m.r .= if kept.items.len == 1 { cells[17] } else { null };
    print(show(m.r), " ", show(kept.r));
    var t1 = Tagged { tag: "t", w: W { r: if c3.v == 3 { cells[18] } else { null }, items: [1] } };
    var t2 = Tagged { tag: "uv", w: make(19) };
    t2.w.items.push(20);
    print(t1.tag, " ", show(t1.w.r), " ", t1.w.items, " ", t2.tag, " ", show(t2.w.r), " ",
          t2.w.items);

    let n0: Cell& = nodes.alloc_ref(Cell { v: 100 });
    let n1: Cell& = nodes.alloc_ref(Cell { v: 101 });
    nodes.free(nodes.index_of(n0));
    let n2: Cell& = nodes.alloc_ref(Cell { v: 102 });
    var h = Holder { first: n2, last: n1, items: [] };
    let last = h.last;
    assert(last);
    print(h.first.v, " ", nodes.index_of(h.first), " ", last.v, " ", nodes.index_of(last));

    var fixeds: Fixed[>..] = [];
    var named: Named[>..] = [];
    var packed: Packed[>..] = [];
    var links: (Cell&<u32 in cells>?)[>..] = [];
    for k in 3 {
        let fx = Fixed { r: if k == 1 { null } else { cells[k + 20] }, n: k };
        fixeds.push(fx);
        named.push(Named { name: "n", r: match k { 0 => cells[25], 1 => null, _ => pick(26) } });
        packed.push(Packed { name: "p", r: if k == 2 { null } else { pick(k + 27) } });
        links.push(if k == 1 { null } else { cells[k + 30] });
        let built: (Cell&<u32 in cells>?)[] = [pick(k + 35), if k == 0 { pick(38) } else { null }];
        print(show(built[0]), " ", show(built[1]));
    }
    links.resize(5, match links.len { 3 => cells[33], _ => null });
    let pair: (Cell&<u16 in cells>?)[2] = [if c3.v == 3 { cells[34] } else { null },
                                           match c3.v { 3 => null, _ => c3 }];
    for x in fixeds { print(show(x.r), " ", x.n); }
    for x in named { print(x.name, " ", show(x.r)); }
    for x in packed { print(x.name, " ", show(x.r)); }
    for x in links { print(show(x)); }
    print(show(pair[0]), " ", show(pair[1]));

    // 8 bytes a cell and the offset biased by one: cell 31 is at 249, the
    // last a u8 reaches, and cell 32 needs 257.
    var s1 = Small { r: cells[31], items: [] };
    print(show(s1.r));
    var s2 = Small { r: if s1.items.len == 0 { cells[32] } else { null }, items: [] };
    print(show(s2.r));   // Not reached.
}
