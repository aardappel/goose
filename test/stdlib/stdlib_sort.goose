// runtime-debug: std's sort and stable_sort over input shapes that take
// their different paths: every size around the insertion-sort and pivot
// thresholds, sorted, reverse-sorted and run-structured input, many and few
// duplicates, an input built against the pivot choice that must end in the
// heapsort fallback, comparators that break the ordering rules, and
// stability. Each result is checked against a heap-sorted copy.
import std;

// The values of xs in order, by a route that shares nothing with sort.
fn reference(xs: i64[:]) -> i64[>..] {
    var h: i64[>..<] = [];
    for x in xs { h.heap_push(x); }
    var out: i64[>..] = [];
    while h.len > 0 { out.push(h.heap_pop()); }
    return out;
}

// Input of length n in one of the shapes below.
fn shape(kind: i64, n: i64, r: rng&) -> i64[>..] {
    var xs: i64[>..] = [];
    for i in n {
        var x = 0;
        if kind == 0 { x = r.rand_int(1000000); }              // random, few repeats
        else if kind == 1 { x = r.rand_int(4); }                // four values
        else if kind == 2 { x = 7; }                             // all equal
        else if kind == 3 { x = i; }                             // sorted
        else if kind == 4 { x = n - i; }                         // strictly descending
        else if kind == 5 { x = (n - i) / 3; }                   // descending with ties
        else if kind == 6 { x = if i < n / 2 { i } else { n - i }; }   // organ pipe
        else if kind == 7 { x = i % 17; }                        // sawtooth
        else if kind == 8 { x = i / 5 + r.rand_int(3); }         // nearly sorted
        else if kind == 9 { x = if i % 64 < 32 { i } else { -i }; }    // runs both ways
        else if kind == 10 { x = r.rand_int(n / 8 + 1) * 1000; } // many groups of repeats
        else { x = i; }                                          // sorted, then...
        xs.push(x);
    }
    if kind == 11 && n > 1 {
        // ...but for a few swapped pairs.
        for k in 3 { swap_at(xs, r.rand_int(n), r.rand_int(n)); }
    }
    return xs;
}

let NSHAPES = 12;

fn sizes() -> i64[>..] {
    var s: i64[>..] = [];
    for n in 70 { s.push(n); }
    for n in [100, 127, 128, 129, 200, 255, 256, 257, 1000, 1023, 4096, 10007] { s.push(n); }
    return s;
}

fn check_unstable() {
    var r = rng { 12345 };
    var bad = 0;
    var runs = 0;
    for kind in NSHAPES {
        for n in sizes() {
            let xs = shape(kind, n, r);
            let want = reference(xs);
            var a = copy(xs);
            sort(a);
            if a != want { bad += 1; print("sort wrong: shape ", kind, " size ", n); }
            // By a block, descending.
            var d = copy(xs);
            sort(d) { p, q => q < p };
            reverse(d);
            if d != want { bad += 1; print("sort by block wrong: shape ", kind, " size ", n); }
            runs += 2;
        }
    }
    print("sort: ", runs, " runs, ", bad, " wrong");
}

struct Item { key: i64, seq: i64 }

fn check_stable() {
    var r = rng { 777 };
    var bad = 0;
    var runs = 0;
    for kind in NSHAPES {
        for n in sizes() {
            let keys = shape(kind, n, r);
            var items: Item[>..] = [];
            for k, i in keys { items.push(Item { k, i }); }
            stable_sort(items) { p, q => p.key < q.key };
            var got: i64[>..] = [];
            var ok = true;
            for it, i in items {
                got.push(it.key);
                // Equal keys keep the order they came in.
                if i > 0 && items[i - 1].key == it.key && items[i - 1].seq > it.seq { ok = false; }
            }
            if got != reference(keys) { ok = false; }
            var plain = copy(keys);
            stable_sort(plain);
            if plain != got { ok = false; }
            if !ok { bad += 1; print("stable_sort wrong: shape ", kind, " size ", n); }
            runs += 1;
        }
    }
    print("stable_sort: ", runs, " runs, ", bad, " wrong");
}

// Sub-slices: only the viewed elements move, and the element in front of
// the slice (smaller than everything in it) is not taken for part of it.
fn check_slices() {
    var r = rng { 99 };
    var xs: i64[>..] = [];
    for i in 3000 { xs.push(r.rand_int(50)); }
    let before = copy(xs);
    sort(xs[1000..2000]);
    stable_sort(xs[2000..2900]);
    var ok = xs[..1000] == before[..1000] && xs[2900..] == before[2900..];
    ok = ok && xs[1000..2000] == reference(before[1000..2000]);
    ok = ok && xs[2000..2900] == reference(before[2000..2900]);
    var ys: i64[>..] = [];
    for i in 500 { ys.push(if i < 100 { 0 } else { 1 + r.rand_int(3) }); }
    let ys0 = copy(ys);
    sort(ys[100..]);
    ok = ok && ys[..100] == [0; 100] && ys[100..] == reference(ys0[100..]);
    print("slices: ", ok);
}

// McIlroy's adversary ("A Killer Adversary for Quicksort", 1999): the
// comparator settles the values only as the sort compares them, always so
// that the pivot candidate comes out small. The values it settles on are an
// input that makes the pivot choice bad at every step; sorting that input
// must still take O(n log n) comparisons, which the heapsort fallback
// guarantees: at most about 4 n log2 n, against n * n / 4 for the
// quicksort alone.
fn killer(n: i64) -> i64[>..] {
    let gas = n;
    var val: i64[>..] = [];
    for i in n { val.push(gas); }
    var idx: i64[>..] = [];
    for i in n { idx.push(i); }
    // The first two settle descending, so that the check for input already
    // in order stops at once and the adversary meets the partitioning.
    val[0] = 1;
    val[1] = 0;
    var solid = 2;
    var candidate = -1;
    sort(idx) { x, y =>
        if val[x] == gas && val[y] == gas {
            if x == candidate { val[x] = solid; } else { val[y] = solid; }
            solid += 1;
        }
        if val[x] == gas { candidate = x; } else if val[y] == gas { candidate = y; }
        val[x] < val[y]
    };
    return val;
}

fn check_adversary() {
    for n in [1000, 20000] {
        let k = killer(n);
        var count = 0;
        var a = copy(k);
        sort(a) { x, y => count += 1; x < y };
        var lg = 0;
        while (1 << lg) < n { lg += 1; }
        print("killer ", n, ": sorted ", a == reference(k), ", within 6 n log2 n compares ",
              count <= 6 * n * lg);
    }
}

// A comparator that is no ordering (answers at random, or `<=`) and NaNs
// under `<` may leave any order, but never anything but a permutation, and
// never abort.
fn check_bad_comparators() {
    var r = rng { 4242 };
    var ok = true;
    for n in [5, 21, 64, 300, 5000] {
        let xs = shape(1, n, r);
        let want = reference(xs);
        var a = copy(xs);
        var coin = rng { n as! u64 };
        sort(a) { x, y => coin.rand_int(2) == 0 };
        sort(a);
        ok = ok && a == want;
        var b = copy(xs);
        stable_sort(b) { x, y => coin.rand_int(2) == 0 };
        stable_sort(b);
        ok = ok && b == want;
        var c = copy(xs);
        sort(c) { x, y => x <= y };
        sort(c);
        ok = ok && c == want;
        var d = copy(xs);
        stable_sort(d) { x, y => x <= y };
        stable_sort(d);
        ok = ok && d == want;
    }
    var f: f64[>..] = [];
    for i in 1000 { f.push(if i % 7 == 0 { 0.0 / 0.0 } else { r.rand_flt() }); }
    var g = copy(f);
    sort(f);
    stable_sort(g);
    var nan = 0;
    for x in f { if x != x { nan += 1; } }
    for x in g { if x != x { nan += 1; } }
    print("bad comparators: ", ok, ", NaNs kept ", nan);
}

// Arrays of other element types and kinds.
fn check_kinds() {
    var lim: u8[..40] = [];
    for i in 40 { lim.push(((i * 37) % 41) as u8); }
    sort(lim);
    var ok = true;
    for i in 1..lim.len { if lim[i - 1] > lim[i] { ok = false; } }
    var fixed: f32[64] = [0.0; 64];
    for i in 64 { fixed[i] = ((i * 29) % 64) * 0.5; }
    stable_sort(fixed) { a, b => b < a };
    for i in 1..64 { if fixed[i - 1] < fixed[i] { ok = false; } }
    var words: u8[][>..] = ["pear", "fig", "apple", "kiwi", "date", "plum", "lime", "nut", "yuzu",
                            "sloe", "cherry", "grape", "melon", "peach", "quince", "lemon",
                            "mango", "guava", "olive", "papaya", "banana", "orange", "apricot"];
    var views: const u8[:][>..] = [];
    for w in words { views.push(w); }
    sort(views) { a, b => compare(a, b) < 0 };
    for i in 1..views.len { if compare(views[i - 1], views[i]) >= 0 { ok = false; } }
    print("kinds: ", ok, " ", views[0], " ", views[views.len - 1]);
}

fn main() {
    check_unstable();
    check_stable();
    check_slices();
    check_adversary();
    check_bad_comparators();
    check_kinds();
}
