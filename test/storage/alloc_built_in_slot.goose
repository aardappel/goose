// runtime-debug: an element allocated in a pool is built in its slot whatever gives it.
// An element holding `in pool` references is built in the slot an
// allocation takes, as a pushed one is, when an if, a match or a block
// gives it as well as when a literal does, so a `self` in it is the slot's
// own position in the pool (§3.9). A relative-reference element stores the
// offset to the reference it is given, not the reference.

struct Node { next: Node&<u32 in pool>, v: i64 }
reusable var pool: Node[>..] = [];

struct Cell { v: i64 }
reusable var cells: Cell[>..] = [];
reusable var refs: (Cell&<u32 in cells>?)[>..] = [];

fn show(i: i64) {
    let r = refs[i];
    guard r else { print(i, ": null"); return; }
    print(i, ": ", r.v, " at ", cells.index_of(r));
}

fn main() {
    pool.push(Node { next: self, v: 10 });
    pool.push(Node { next: self, v: 11 });
    let c = pool.len > 1;
    let i = pool.alloc_index(if c { Node { next: self, v: 1 } } else { Node { next: self, v: 2 } });
    print(i, " ", pool[i].next.v, " ", pool.index_of(pool[i].next));            // 2 1 2
    let r .= pool.alloc_ref({ Node { next: self, v: 3 } });
    print(r.v, " ", r.next.v, " ", pool.index_of(r.next));                       // 3 3 3
    // Slots off the freelist.
    pool.free(1);
    let r2 .= pool.alloc_ref(if c { Node { next: self, v: 4 } } else { Node { next: pool[0], v: 5 } });
    print(pool.index_of(r2), " ", r2.next.v, " ", pool.index_of(r2.next), " ", pool.len);   // 1 4 1 4
    pool.free(0);
    let j = pool.alloc_index(match i { 2 => Node { next: self, v: 6 }, _ => Node { next: pool[1], v: 7 } });
    print(j, " ", pool[j].next.v, " ", pool.index_of(pool[j].next));            // 0 6 0
    for n in pool { print(n.v, " -> ", pool.index_of(n.next)); }

    let a = cells.alloc_index(Cell { v: 1 });
    let b = cells.alloc_index(Cell { v: 2 });
    let x = refs.alloc_index(cells[b]);
    let y = refs.alloc_index(if c { cells[a] } else { null });
    let z = refs.alloc_index(if c { null } else { cells[a] });
    show(x);                                                                     // 0: 2 at 1
    show(y);                                                                     // 1: 1 at 0
    show(z);                                                                     // 2: null
    refs.free(x);
    let w = refs.alloc_index(cells[a]);
    show(w);                                                                     // 0: 1 at 0
    print(refs.len);                                                             // 3
}
