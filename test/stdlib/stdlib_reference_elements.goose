// std's functions that move elements reorder an array of references
// without writing through them (spec §3.8): they store whole elements with
// `.=`, which generic code may use on any type (§3.8), and hold one in a
// `T` local, which keeps the reference.
import std;

// A node holding self-relative references, which can be neither copied nor
// assigned whole (§3.9): only references to it move.
struct Cell { id: i64, links: (Cell&<u32>)[..2] }

// User generic code moves elements the same way, values and references
// alike.
fn rotate_left<T>(xs: T[:]) {
    if xs.len < 2 { return; }
    let first: T = xs[0];
    for i in xs.len - 1 { xs[i] .= xs[i + 1]; }
    xs[xs.len - 1] .= first;
}

fn main() {
    var xs: i64[6] = [50, 20, 60, 10, 40, 30];
    var refs: i64&[>..] = [];
    for i in 6 { refs.push(xs[i]); }

    reverse(refs);
    print(xs, " ", refs);
    sort(refs);
    print(xs, " ", refs);
    sort(refs) { a, b => a > b };
    print(xs, " ", refs);
    swap_at(refs, 0, 5);
    print(xs, " ", refs);
    var r = rng { s: 7 };
    shuffle(refs, r);
    sort(refs);
    print(xs, " ", refs);
    rotate_left(refs);
    print(xs, " ", refs);
    // The elements are still the references into xs: writing through one
    // writes xs.
    refs[0] += 1;
    print(xs, " ", refs);

    var others: i64&[6] = [xs[0], xs[0], xs[0], xs[0], xs[0], xs[0]];
    copy_into(others, refs);
    print(xs, " ", others);
    fill(others, xs[5]);
    print(xs, " ", others);

    insert_at(refs, 1, &xs[5]);
    print(xs, " ", refs);
    let removed = remove_at(refs, 0);
    let swapped = swap_remove(refs, 1);
    print(removed, " ", swapped, " ", xs, " ", refs);
    retain(refs) { it > 25 };
    print(xs, " ", refs);
    sort(refs);
    refs.push(xs[1]);
    dedup(refs);
    print(xs, " ", refs);

    var heap: i64&[>..] = [];
    for i in 6 { heap_push(heap, &xs[i]); }
    var popped: i64[>..] = [];
    while heap.len > 0 { popped.push(heap_pop(heap)); }
    print(xs, " ", popped);
    heap.append(refs);
    make_heap(heap) { a, b => a > b };
    print(xs, " ", heap[0]);

    // Values: the same functions, the same results as ever.
    var vals = [5, 3, 9, 1];
    reverse(vals);
    rotate_left(vals);
    print(vals);
    sort(vals);
    fill(vals[2..], 0);
    print(vals);

    // References to cells with self-relative links reorder without copying
    // a cell.
    var cells: Cell[>..] = [];
    for i in 3 { cells.push(Cell { id: i, links: [] }); }
    cells[0].links.push(cells[2]);
    var order: Cell&[>..] = [];
    for &c in cells { order.push(c); }
    reverse(order);
    swap_at(order, 0, 1);
    for c in order { print(c.id, " ", c.links.len); }
    print(cells[0].links[0].id);
}
