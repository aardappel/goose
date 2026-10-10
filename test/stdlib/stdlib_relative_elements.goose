// std's functions that move elements reorder arrays of relative references
// (§3.9) as they do arrays of plain ones (stdlib_reference_elements.goose).
// The `T` locals and parameters they move an element through hold the plain
// reference, as a `T` result does, and storing it back into an element
// encodes it again: measured from that element's slot for a self-relative
// link, which must point within the slot's root, and from the pool's base
// for an `in pool` one.
import std;

// Self-relative links between elements of one array.
struct Cell { v: i64, links: (Cell&<u32>)[..8] }
// Self-relative links to the fields of the value holding them.
struct Box { vals: i64[6], idx: (i64&<u16>)[..8] }

struct P { v: i64 }
var pool: P[>..] = [];
struct Q { links: (P&<u32 in pool>)[..8] }

fn show(xs: (Cell&<u32>)[:]) {
    var out: i64[>..] = [];
    for c in xs { out.push(c.v); }
    print(out);
}

fn show_pool(xs: (P&<u32 in pool>)[:]) {
    var out: i64[>..] = [];
    for p in xs { out.push(p.v); }
    print(out);
}

// User generic code moves elements through `T` locals and parameters.
fn rotate_left<T>(xs: T[:]) {
    if xs.len < 2 { return; }
    let first: T = xs[0];
    for i in xs.len - 1 { xs[i] .= xs[i + 1]; }
    xs[xs.len - 1] .= first;
}

fn put_last<T>(xs: T[:], v: T) { xs[xs.len - 1] .= v; }

// Links reached through a parameter: its class is the links' root.
fn by_value_order(c: Cell&) {
    sort(c.links) { a, b => a.v < b.v };
    swap_at(c.links, 0, c.links.len - 1);
}

fn main() {
    var cells: Cell[>..] = [];
    for i in 6 { cells.push(Cell { v: (i * 7 + 3) % 6, links: [] }); }
    for i in 6 { cells[0].links.push(cells[i]); }
    show(cells[0].links);
    swap_at(cells[0].links, 0, 1);
    show(cells[0].links);
    reverse(cells[0].links);
    show(cells[0].links);
    var r = rng { s: 7 };
    shuffle(cells[0].links, r);
    sort(cells[0].links) { a, b => a.v < b.v };
    show(cells[0].links);
    sort(cells[0].links) { a, b => a.v > b.v };
    show(cells[0].links);
    rotate_left(cells[0].links);
    show(cells[0].links);
    put_last(cells[0].links, cells[3]);
    show(cells[0].links);
    fill(cells[0].links[3..], cells[1]);
    show(cells[0].links);
    copy_into(cells[0].links[..2], cells[0].links[3..5]);
    show(cells[0].links);
    insert_at(cells[0].links, 1, &cells[2]);
    show(cells[0].links);
    retain(cells[0].links) { it.v != 4 };
    show(cells[0].links);
    for i in 6 { cells[1].links.push(cells[5 - i]); }
    by_value_order(cells[1]);
    show(cells[1].links);
    make_heap(cells[1].links) { a, b => a.v < b.v };
    print(cells[1].links[0].v);
    cells[1].links.clear();
    for i in 6 { heap_push(cells[1].links, &cells[(i * 5) % 6]) { a, b => a.v > b.v }; }
    print(cells[1].links[0].v);

    // The moved links are offsets from their own slots: a copy of the whole
    // array's image leads to the same elements of the copy.
    var img: u8[>..] = cells.to_bytes();
    var back, ok = from_bytes<Cell[>..]>(img);
    var at: i64[>..] = [];
    var backat: i64[>..] = [];
    for c in cells[0].links { at.push(cells.index_of(c)); }
    for c in back[0].links { backat.push(back.index_of(c)); }
    print(ok, " ", at, " ", backat);
    show(back[1].links);

    // Links to fields, pointing at integers: sorted by `<`, and popped as a
    // copy of the pointee.
    var b = Box { vals: [5, 2, 4, 0, 3, 1], idx: [] };
    for i in 6 { b.idx.push(b.vals[i]); }
    sort(b.idx);
    print(b.vals, " ", b.idx);
    reverse(b.idx);
    rotate_left(b.idx);
    print(b.vals, " ", b.idx);
    print(remove_at(b.idx, 0), " ", swap_remove(b.idx, 0), " ", b.idx);
    b.idx.push(b.vals[1]);
    sort(b.idx);
    dedup(b.idx);
    print(b.idx);
    make_heap(b.idx);
    var popped: i64[>..] = [];
    while b.idx.len > 0 { popped.push(heap_pop(b.idx)); }
    print(popped);
    for i in 6 { heap_push(b.idx, &b.vals[i]); }
    while b.idx.len > 0 { popped.push(heap_pop(b.idx)); }
    print(popped);
    // Still the references into vals: writing through one writes vals.
    b.idx.push(b.vals[0]);
    let first .= b.idx[0];
    first += 10;
    print(b.vals);

    // `in pool` links, in a limited array and in a grow-only one.
    for i in 24 { pool.push(P { v: (i * 7 + 3) % 24 }); }
    var q = Q { links: [] };
    for i in 6 { q.links.push(pool[i]); }
    swap_at(q.links, 0, 5);
    reverse(q.links);
    show_pool(q.links);
    sort(q.links) { a, b => a.v < b.v };
    show_pool(q.links);
    rotate_left(q.links);
    put_last(q.links, pool[23]);
    fill(q.links[..2], pool[1]);
    show_pool(q.links);
    var arr: (P&<u32 in pool>)[>..] = [];
    for i in 24 { arr.push(pool[i]); }
    stable_sort(arr) { a, b => a.v / 4 < b.v / 4 };
    show_pool(arr);
    sort(arr) { a, b => a.v > b.v };
    show_pool(arr[..6]);
    make_heap(arr) { a, b => a.v < b.v };
    var firsts: i64[>..] = [];
    for i in 4 { firsts.push(heap_pop(arr) { a, b => a.v < b.v }.v); }
    heap_push(arr, &pool[0]) { a, b => a.v < b.v };
    print(firsts, " ", arr[0].v, " ", arr.len);
}
