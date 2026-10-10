// runtime-debug: std's binary heap (heap_push, heap_pop, make_heap, with and
// without a comparator) against a reference that swaps the element along,
// as std did before its sifts moved a hole. For a strict weak order every
// operation must leave the same layout, ties included, and the heap
// property after every step. Each line prints a mismatch count (0) or a
// layout checked by hand.
import std;

struct Item { key: i64, id: i64 }

struct Cell { v: i64 }

fn by_key(a: Item, b: Item) -> bool { a.key < b.key }

fn ref_heap_push<A, T, F>(xs: A&, v: T) {
    xs.push(v);
    var i = xs.len - 1;
    while i > 0 {
        let p = (i - 1) / 2;
        if F(xs[i], xs[p]) {
            swap_at(xs, i, p);
            i = p;
        } else {
            break;
        }
    }
}

fn ref_sift_down<T, F>(xs: T[:], start: i64) {
    let n = xs.len;
    var i = start;
    loop {
        let l = 2 * i + 1;
        if l >= n { break; }
        var m = l;
        let r = l + 1;
        if r < n && F(xs[r], xs[l]) { m = r; }
        if F(xs[m], xs[i]) {
            swap_at(xs, i, m);
            i = m;
        } else {
            break;
        }
    }
}

// Moves the last element itself to the root, as std's element-moving
// functions do (`.=`), so an array of references is reordered, not
// written through.
fn ref_heap_pop<A, F>(xs: A&) {
    let top = xs[0];
    let n = xs.len - 1;
    xs[0] .= xs[n];
    ref_sift_down(xs[..n], 0) { a, b => F(a, b) };
    xs.pop();
    return top;
}

fn ref_make_heap<A, F>(xs: A&) {
    var i = xs.len / 2;
    while i > 0 {
        i -= 1;
        ref_sift_down(xs, i) { a, b => F(a, b) };
    }
}

fn is_heap<T, F>(xs: T[:]) -> bool {
    for i in 1..xs.len {
        if F(xs[i], xs[(i - 1) / 2]) { return false; }
    }
    return true;
}

// Pushes and pops at random, mostly pushes in the first half and mostly pops
// in the second, then drains; keys from a small range make many ties, which
// the ids tell apart.
fn mixed_items(seed: u64, ops: i64, keys: i64) -> i64 {
    var r = rng { seed };
    var h: Item[>..<] = [];
    var g: Item[>..<] = [];
    var bad = 0;
    for k in ops {
        let pushes = if k < ops / 2 { 7 } else { 3 };
        if h.len == 0 || rand_int(r, 10) < pushes {
            let it = Item { key: rand_int(r, keys), id: k };
            heap_push(h, it) { a, b => a.key < b.key };
            ref_heap_push(g, it) { a, b => a.key < b.key };
        } else {
            let x = heap_pop(h) { a, b => a.key < b.key };
            let y = ref_heap_pop(g) { a, b => a.key < b.key };
            if !(x == y) { bad += 1; }
        }
        let ok = is_heap(h) { a, b => a.key < b.key };
        if !(h == g) || !ok { bad += 1; }
    }
    while h.len > 0 {
        if !(heap_pop(h, by_key) == ref_heap_pop(g, by_key)) { bad += 1; }
        if !(h == g) || !is_heap(h, by_key) { bad += 1; }
    }
    return bad + g.len;
}

// The forms without a block, on plain integers, against the reference by <.
fn mixed_ints(seed: u64, ops: i64) -> i64 {
    var r = rng { seed };
    var h: i64[>..<] = [];
    var g: i64[>..<] = [];
    var bad = 0;
    for k in ops {
        if h.len == 0 || rand_int(r, 5) < 3 {
            let v = rand_int(r, 50) - 25;
            heap_push(h, v);
            ref_heap_push(g, v) { a, b => a < b };
        } else {
            let y = ref_heap_pop(g) { a, b => a < b };
            if heap_pop(h) != y { bad += 1; }
        }
        let ok = is_heap(h) { a, b => a < b };
        if !(h == g) || !ok { bad += 1; }
    }
    return bad;
}

// make_heap at every size up to 40, as a min-heap and as a max-heap.
fn make_heaps(seed: u64) -> i64 {
    var r = rng { seed };
    var bad = 0;
    for n in 41 {
        var a: i64[>..<] = [];
        for _ in n { a.push(rand_int(r, 12)); }
        var b: i64[>..<] = copy(a);
        var c: i64[>..<] = copy(a);
        var d: i64[>..<] = copy(a);
        make_heap(a);
        ref_make_heap(b) { x, y => x < y };
        make_heap(c) { x, y => y < x };
        ref_make_heap(d) { x, y => y < x };
        let oka = is_heap(a) { x, y => x < y };
        let okc = is_heap(c) { x, y => y < x };
        if !(a == b) || !oka { bad += 1; }
        if !(c == d) || !okc { bad += 1; }
    }
    return bad;
}

// Popping everything gives the pushed values in order.
fn pop_order(seed: u64, n: i64) -> i64 {
    var r = rng { seed };
    var h: i64[>..<] = [];
    var all: i64[>..] = [];
    for _ in n {
        let v = rand_int(r, 1000);
        heap_push(h, v);
        all.push(v);
    }
    sort(all);
    var bad = 0;
    for i in n {
        if heap_pop(h) != all[i] { bad += 1; }
    }
    return bad + h.len;
}

// How often each version calls the comparator over one random run: pushes
// make the same comparisons, pops fewer.
fn comparisons(seed: u64, n: i64) {
    var r = rng { seed };
    var h: i64[>..<] = [];
    var g: i64[>..<] = [];
    var hc = 0;
    var gc = 0;
    for _ in n {
        let v = rand_int(r, 100000);
        heap_push(h, v) { a, b => hc += 1; a < b };
        ref_heap_push(g, v) { a, b => gc += 1; a < b };
    }
    print(hc == gc, " ", hc > 0);
    hc = 0;
    gc = 0;
    while h.len > 0 {
        heap_pop(h) { a, b => hc += 1; a < b };
        ref_heap_pop(g) { a, b => gc += 1; a < b };
    }
    print(hc < gc, " ", hc > 0);
}

// One to three elements, step by step.
fn small() {
    var h: i64[>..<] = [];
    heap_push(h, 2);
    print(h);                                      // [2]
    print(heap_pop(h), " ", h.len);                // 2 0
    heap_push(h, 5);
    heap_push(h, 4);
    print(h);                                      // [4, 5]
    heap_push(h, 3);
    print(h);                                      // [3, 5, 4]
    print(heap_pop(h), " ", h);                    // 3 [4, 5]
    print(heap_pop(h), " ", h);                    // 4 [5]
    heap_push(h, 1);
    print(h);                                      // [1, 5]
    print(heap_pop(h), heap_pop(h), " ", h.len);   // 15 0
    var m: i64[>..<] = [1, 2];
    make_heap(m) { a, b => b < a };
    print(m);                                      // [2, 1]
    var one: i64[>..<] = [7];
    make_heap(one);
    print(one);                                    // [7]
    var none: i64[>..<] = [];
    make_heap(none);
    print(none.len);                               // 0
    var w: i64[>..<] = [];
    let s: i32 = 3;
    heap_push(w, s);                               // An i32 pushed into i64s.
    heap_push(w, 1);
    print(heap_pop(w), heap_pop(w));               // 13
}

// A heap in a limited array, and a sequence whose layouts were worked out
// by hand: pushes sift up, pops refill the root along the smaller child.
fn layouts() {
    var lim: i64[..8] = [];
    for v in [6, 2, 9, 4, 1, 8, 5] { heap_push(lim, v); }
    print(lim);                                    // [1, 2, 5, 6, 4, 9, 8]
    print(heap_pop(lim), " ", lim);                // 1 [2, 4, 5, 6, 8, 9]
    print(heap_pop(lim), " ", lim);                // 2 [4, 6, 5, 9, 8]
    heap_push(lim, 3);
    print(lim);                                    // [3, 6, 4, 9, 8, 5]
}

// References in the heap: std's algorithms move the references themselves,
// as the reference version does too, and leave the cells they point to
// alone.
fn references() -> i64 {
    var c1: Cell[>..] = [];
    var c2: Cell[>..] = [];
    for i in 12 {
        c1.push(Cell { v: (i * 7) % 12 });
        c2.push(Cell { v: (i * 7) % 12 });
    }
    var h: Cell&[>..<] = [];
    var g: Cell&[>..<] = [];
    var bad = 0;
    for i in 12 {
        heap_push(h, &c1[i]) { a, b => a.v < b.v };
        ref_heap_push(g, &c2[i]) { a, b => a.v < b.v };
    }
    for _ in 5 {
        let x = heap_pop(h) { a, b => a.v < b.v };
        let y = ref_heap_pop(g) { a, b => a.v < b.v };
        if x.v != y.v { bad += 1; }
    }
    for i in 12 {
        if c1[i].v != (i * 7) % 12 || c2[i].v != (i * 7) % 12 { bad += 1; }
    }
    return bad;
}

fn main() {
    var bad = 0;
    for seed in 20 {
        bad += mixed_items(seed as u64, 300, 6);
        bad += mixed_items(seed as u64 + 100, 120, 1000);
        bad += mixed_ints(seed as u64, 200);
    }
    print(bad);                                    // 0
    print(make_heaps(5) + make_heaps(6));          // 0
    print(pop_order(9, 500));                      // 0
    comparisons(3, 2000);                          // true true / true true
    small();
    layouts();
    print(references());                           // 0
}
