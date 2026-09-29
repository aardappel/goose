// A reference or slice read out of a field, an element or a global never
// points into a grow-shrink array's elements, since none is ever stored there
// (§5.2). So the read-back rule (§9.5) leaves every variable whose
// grow-shrink array could hold the pointee out of such a read's candidates,
// and the store rule passes over the read where what is left only bounds it.
// Every use below was rejected as if the read might point into the
// grow-shrink array beside it: a read stored, bound first, passed to a
// function storing its parameter and returned by one; index_of and a
// pool-relative link, which need the one array the read names; a local
// container's read stored into a global, which only the grow-shrink local
// kept from outliving it; a call shrinking the array while the read is live;
// a read through a parameter whose argument is such an array's element; a
// view out of a for binder; and a slice in a field, named by a reference. A
// global var's read, which any function may bind (§9.5), leaves the same
// arrays out: the link and index_of need that too.

struct Cell { v: i64 }
struct Row { r: Cell&, c: Cell }
struct Entry { text: u8[:] }
struct Link { to: Cell&<u32 in cells> }
struct Named { rs: u8[:]& }

var cells: Cell[>..] = [Cell { v: 10 }, Cell { v: 11 }, Cell { v: 12 }];
var scratch: Cell[>..<] = [Cell { v: 90 }];
var slots: (Cell&)[>..] = [];
var other: (Cell&)[>..] = [];
var links: Link[>..] = [];
var chars: u8[>..] = [];
var related: u8[>..<] = [];
var entries: Entry[>..] = [];
var views: (u8[:])[>..] = [];
var kept: (u8[:])[>..] = [];
var anywhere: Cell& = cells[2];

fn copy_first() {
    if slots.len > 0 { other.push(slots[0]); }
}

fn keep(x: Cell&) { other.push(x); }

fn same(x: Cell&) -> Cell& { return x; }

// related shrinks while v is live.
fn trim(v: u8[:]) -> i64 {
    related.clear();
    v.len
}

// e is an element of a grow-shrink array, which bounds what e.r points at.
fn first_of(e: Row&) -> i64 {
    var loc: (Cell&)[>..] = [];
    loc.push(e.r);
    loc[0].v
}

fn link_global() {
    links.push(Link { to: anywhere });
    print(cells.index_of(anywhere), " ", links[1].to.v);
}

fn names_of() {
    var ents: Entry[>..] = [];
    var spare: u8[>..<] = [];
    spare.push(7);
    ents.push(Entry { text: chars[1..3] });
    var named: Named[>..] = [];
    let r = &ents[0].text;
    named.push(Named { rs: r });
    let v = r;
    spare.clear();
    print(named[0].rs, " ", v, " ", spare.len);
}

fn main() {
    slots.push(cells[1]);
    slots.push(cells[2]);
    copy_first();
    let r: Cell& = slots[1];
    other.push(r);
    keep(slots[0]);
    other.push(same(slots[1]));
    for o in other { print(o.v); }
    print(cells.index_of(slots[0]), " ", cells.index_of(slots[1]));
    links.push(Link { to: slots[1] });
    print(links[0].to.v);
    link_global();
    var rows: Row[>..<] = [Row { r: cells[0], c: Cell { v: 20 } }];
    other.push(rows[0].r);
    print(other[4].v, " ", first_of(rows[0]));
    chars.append("abc");
    related.push(99);
    entries.push(Entry { text: chars[0..2] });
    print(trim(entries[0].text), " ", related.len);
    views.push(chars[1..3]);
    for w in views { kept.push(w); }
    print(kept[0]);
    names_of();
    print(scratch[0].v);
}
