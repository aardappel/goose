// Any function may bind a global `var` of reference or slice type, so a read
// of one in a function's body is the read-back rule's answer for a global
// (§9.5), whichever functions were checked first. That answer is still exact
// for a global naming a pool, which points into that pool, and for one whose
// pointee only one global can own, and a global `let` keeps its binding: the
// links, index_of and the store below need no more. A global initializer
// runs once, where the bindings so far are all there are: there a global
// `var` reads as bound, or as null before any binding, so a pop through one
// and an assignment of the array beside it see only the array it is bound
// to, and a read-back needs no bound for the globals declared later. A read
// is as writable as the global's type, and a slot read (§5.2): a grow-shrink
// global that could own the pointee does not taint it, and a pop of one
// leaves a byte view of another array alone.

struct Cell { v: i64 }
var cells: Cell[>..] = [Cell { v: 10 }, Cell { v: 11 }, Cell { v: 12 }];
var spare: Cell[>..] = [Cell { v: 20 }];
var scratch: Cell[>..<] = [Cell { v: 40 }];
var pile: Cell[>..<] = [Cell { v: 50 }, Cell { v: 51 }];
var top: Cell[>..<]& = pile;
var trimmed = {
    let r = &scratch[0];
    top.pop();
    r.v
};
var refilled = {
    scratch = [Cell { v: top[0].v + 1 }];
    scratch[0].v
};
struct Link { to: Cell&<u16 in cells>?, n: i64 }

// Only items can own an Item.
struct Item { w: i64 }
var items: Item[>..] = [Item { w: 1 }, Item { w: 2 }, Item { w: 3 }];
struct Pick { to: Item&<u8 in items> }
var held: (Item&)[>..] = [items[1]];
var early_pick = Pick { to: held[0] };

var anywhere: Cell&? = cells[1];
var early = Link { to: anywhere, n: 1 };
var pending: Cell&? = null;
var none = Link { to: pending, n: 2 };

var cur: Cell&<u32 in cells> = cells[0];
let first: Cell& = cells[0];
var sel: Item& = items[0];
var links: Link[>..] = [];
var picks: Pick[>..] = [];
var seen: (Cell&?)[>..] = [];
var bytes: u8[>..<] = [1, 2, 3];
var ints: i64[>..] = [258];
var view: const u8[:] = "x";

fn link_all() {
    links.push(Link { to: cur, n: 3 });
    links.push(Link { to: first, n: 4 });
    picks.push(Pick { to: sel });
    seen.push(anywhere);
    print(cells.index_of(cur), " ", cells.index_of(first), " ", items.index_of(sel));
}

// Checked before anything binds pending.
fn bump() {
    if pending { pending.v += 100; }
}

fn retarget(i: i64) {
    cur .= cells[i];
    sel .= items[i];
}

fn wander() {
    anywhere .= spare[0];
    pending .= cells[2];
}

fn over() { view = bytes_of(ints); }

fn trim_bytes() -> i64 {
    let v = view;
    bytes.pop();
    v.len
}

fn show(l: Link&) {
    let t = l.to;
    if t { print(l.n, ": ", t.v); } else { print(l.n, ": null"); }
}

fn main() {
    link_all();
    bump();
    retarget(2);
    wander();
    link_all();
    bump();
    for l in links { show(l); }
    show(early);
    show(none);
    for p in picks { print("item ", p.to.w); }
    print("early item ", early_pick.to.w);
    for s in seen {
        if s { print("seen ", s.v); }
    }
    print(cells[2].v, " ", trimmed, " ", refilled, " ", pile.len);
    over();
    print(trim_bytes(), " ", bytes.len);
}
