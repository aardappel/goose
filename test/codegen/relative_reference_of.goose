// `&` of a location holding a relative reference is the reference it holds
// (§3.8), loaded (§3.9): an `in pool` field at every fixed width and the
// varint one, optional or not, a self-relative field of a pool element, and
// an element of an array of links, reached directly and through a parameter,
// bound by `let` and `var`, passed to a block and to an untyped parameter,
// and compared by identity. The loaded reference writes through to its
// pointee, and one read out of `in pool` is rooted at the pool.
struct Cell { v: i64 }
var cells: Cell[>..] = [];

struct S {
    a: Cell&<u8 in cells>?, b: Cell&<u16 in cells>, c: Cell&<u32 in cells>?,
    d: Cell&<u64 in cells>, e: Cell&<varint in cells>?
}
struct Node { v: i64, next: Node&<u8>? }

fn each_link<F>(s: S&) { F(&s.a); F(&s.c); F(&s.e); }
fn each_plain<F>(s: S&) { F(&s.b); F(&s.d); }
fn bump(x, by: i64) { x.v += by; }
fn bump_opt(x, by: i64) { if x { x.v += by; } }
fn at(s: S&) -> i64 {
    let r = &s.d;
    index_of(cells, r)
}
fn same(s: S&, i: i64) -> bool { &s.c .== cells[i] }

fn main() {
    while cells.len < 12 { cells.push(Cell { v: 100 + cells.len }); }
    var s = S { cells[1], cells[2], cells[3], cells[4], cells[5] };
    let a = &s.a;
    var b = &s.b;
    let e = &s.e;
    if a { a.v += 1000; }
    b.v += 2000;
    if e { e.v += 5000; }
    b .= cells[6];
    b.v += 6000;
    print(cells[1].v, " ", cells[2].v, " ", cells[5].v, " ", cells[6].v, " ", s.b.v);

    s.each_link() { r => if r { r.v += 10; } };
    s.each_plain() { r => r.v += 20; };
    bump_opt(&s.c, 300);
    bump(&s.d, 400);
    print(cells[1].v, " ", cells[2].v, " ", cells[3].v, " ", cells[4].v, " ", cells[5].v);
    print(at(s), " ", same(s, 3), " ", same(s, 1), " ", &s.a .== cells[1]);
    s.c .= null;
    let c = &s.c;
    print(c == null, " ", same(s, 3));

    var links: (Cell&<u32 in cells>)[>..] = [cells[7], cells[8]];
    let l = &links[1];
    l.v += 800;
    bump(&links[0], 700);
    print(cells[7].v, " ", cells[8].v, " ", index_of(cells, l));

    var nodes: Node[>..] = [Node { v: 1 }, Node { v: 2 }, Node { v: 3 }];
    nodes[0].next .= nodes[2];
    let n = &nodes[0].next;
    if n { n.v += 30; print(nodes[2].v, " ", index_of(nodes, n)); }
}
