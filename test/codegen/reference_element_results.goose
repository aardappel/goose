// push and alloc_ref return a reference to the element they add (§3.3), but
// there are no references to references (§3.8): an element that is itself a
// reference gives the one it holds, loaded where it is relative, as `&` of
// the element does. It points where the element does, not into the array,
// so writing through it writes the target, and the array may shrink while
// it is held. Generic code returning `T&` from a push takes such arrays too.

struct Cell { v: i64 }
var cells: Cell[4] = [Cell { v: 1 }, Cell { v: 2 }, Cell { v: 3 }, Cell { v: 4 }];
var grow: (Cell&)[>..] = [];
reusable var refs: (Cell&?)[>..] = [];
reusable var cpool: Cell[>..] = [];
var rels: (Cell&<u32 in cpool>)[>..] = [];
var vrels: (Cell&<varint in cpool>?)[>..] = [];

struct Nest { nodes: Cell[..4], links: (Cell&<u16>?)[..4] }

fn add<T>(xs: T[>..]&, v: T) -> T& {
    return xs.push(v);
}

fn nested() {
    var n = Nest { nodes: [], links: [] };
    n.nodes.push(Cell { v: 7 });
    let l = n.links.push(n.nodes[0]);
    if l { l.v = 70; }
    print(n.nodes[0].v, " ", n.links.len);                          // 70 1
}

fn shrinking() {
    var gs: (Cell&)[>..<] = [];
    let x .= gs.push(cells[1]);
    gs.pop();
    x.v += 100;
    print(cells[1].v, " ", gs.len);                                 // 102 0
}

fn main() {
    let a = grow.push(cells[1]);
    let b .= grow.push(cells[2]);
    b.v = 30;
    grow.push(cells[3]).v = 40;
    print(a.v, " ", cells[2].v, " ", cells[3].v, " ", grow.len);    // 2 30 40 3
    let r = refs.alloc_ref(cells[0]);
    if r { r.v = 10; }
    refs.free(0);
    let r2 = refs.alloc_ref(null);
    print(cells[0].v, " ", r2 .== null, " ", refs.len);             // 10 true 1
    var lim: (Cell&)[..4] = [];
    let q .= lim.push(cells[0]);
    q.v += 1;
    print(cells[0].v, " ", lim.len);                                // 11 1
    let ci = cpool.alloc_index(Cell { v: 5 });
    let rr .= rels.push(cpool[ci]);
    rr.v = 50;
    print(cpool[ci].v, " ", cpool.index_of(rels.push(cpool[ci])), " ", rels.len);   // 50 0 2
    let vr = vrels.push(cpool[ci]);
    if vr { print(vr.v); }                                          // 50
    let g .= add(grow, cells[2]);
    g.v = 33;
    print(cells[2].v, " ", grow.len);                               // 33 4
    nested();
    shrinking();
}
