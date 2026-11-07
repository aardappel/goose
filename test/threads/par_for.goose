// goose-threads
// par for (docs/design/child_threads.md §4-§5): the query engine's five
// phases, the blur, and every form the loop takes. Each prints the same at
// -O0 and -O2, with the pool at GOOSE_THREADS=1, where every par for runs in
// place, and at 4, and through TinyCC, which runs them all in place.

import std;

struct Slot { key: i64, val: i64 }

let P = 4;

// Scan and filter: each iteration reads its chunk of the column, writes the
// survivors into the same chunk of the output and reports how many.
fn filter(vals: const i64[:], keep: i64[:], counts: i64[:], threshold: i64) {
    assert(keep.len == vals.len && counts.len == P);
    par for k in P {
        let src = vals.chunk(k, P);             // a const view: R1
        let dst = keep.chunk(k, P);             // this iteration's run of keep: R2
        var n = 0;
        for v in src { if v > threshold { dst[n] = v; n++; } }
        counts[k] = n;                          // one element per iteration: R2
    }
}

fn slot_of(key: i64, cap: i64) -> i64 { ((hash(key) >> 1) as! i64) % cap }

// Probe: the table is read by every iteration at once and written by none.
fn probe(keys: const i64[:], table: const Slot[:], out: i64[:], found: i64[:]) {
    assert(out.len == keys.len && found.len == P && table.len > 0);
    par for k in P {
        let ks = keys.chunk(k, P);
        let os = out.chunk(k, P);
        var n = 0;
        for key in ks {
            var h = slot_of(key, table.len);
            while table[h].key != 0 && table[h].key != key { h = (h + 1) % table.len; }
            if table[h].key == key { os[n] = table[h].val; n++; }
        }
        found[k] = n;
    }
}

fn partition_of(r: i64, parts: i64) -> i64 { r % parts }

// Partitioned build: the partitions of one table are the runs between the
// offsets, each filled by its own iteration.
fn build(rows: const i64[:], table: Slot[:], bounds: const i64[:]) {
    par for k in bounds.len - 1 {
        let mine = table.chunk_at(bounds, k);   // bounds checked once, before the fork
        let cap = mine.len;
        for r in rows {
            if partition_of(r, bounds.len - 1) != k { continue; }
            var h = slot_of(r, cap);
            while mine[h].key != 0 { h = (h + 1) % cap; }
            mine[h] = Slot { key: r, val: r * 10 };
        }
    }
}

// A stackful body: a local table per iteration, growable, on the stacks of
// the worker running the chunk, with a fixed-size summary per iteration.
fn count_distinct(rows: const i64[:], bits: i64, distinct: i64[:]) {
    assert(distinct.len == P);
    par for k in P {
        let mine = rows.chunk(k, P);
        var table: Slot[>..] = [];
        table.resize(1 << bits, Slot { key: 0, val: 0 });
        let mask = (1 << bits) - 1;
        for r in mine {
            var h = ((hash(r) >> 1) as! i64) & mask;
            while table[h].key != 0 && table[h].key != r { h = (h + 1) & mask; }
            table[h] = Slot { key: r, val: r };
        }
        var n = 0;
        for s in table { if s.key != 0 { n++; } }
        distinct[k] = n;
    }
}

// Merge: the count is an integer reduction, the float sum comes back per
// chunk, so that its order is the program's.
fn totals(a: const f64[:], b: const f64[:], partial: f64[:]) -> i64 {
    assert(a.len == b.len && partial.len == P);
    var nonzero = 0;                            // exact in any order
    par for k in P {
        let xs = a.chunk(k, P);
        let ys = b.chunk(k, P);
        var acc = 0.0;
        for i in xs.len { acc += xs[i] * ys[i]; if xs[i] != 0.0 { nonzero++; } }
        partial[k] = acc;
    }
    return nonzero;
}

let W = 12;
let H = 8;

// The blur of samples/22_image_filters.goose, a row of the output per
// iteration: dst.chunk(y, H) is row y of a W * H image.
fn blur(src: const u8[:], dst: u8[:]) {
    assert(src.len == W * H && dst.len == W * H);
    par for y in H {
        let d = dst.chunk(y, H);
        let b = src[y * W..(y + 1) * W];
        if y == 0 || y == H - 1 {
            for x in W { d[x] = b[x]; }
            continue;                           // ends this iteration only
        }
        let a = src[(y - 1) * W..y * W];
        let c = src[(y + 1) * W..(y + 2) * W];
        d[0] = b[0];
        d[W - 1] = b[W - 1];
        for x in 1..W - 1 {
            let s = a[x - 1] as u16 + a[x] + a[x + 1] + b[x - 1] + b[x] + b[x + 1]
                  + c[x - 1] + c[x] + c[x + 1];
            d[x] = (s / 9) as! u8;
        }
    }
}

// `par for &x, i in arr`: iteration i owns arr[i].
fn scale(xs: i64[:], f: i64) {
    par for &x, i in xs { x = x * f + i; }
}

// `par for x in arr`, a reduction by ^=.
fn checksum(xs: const i64[:]) -> u64 {
    var h: u64 = 0;
    par for x in xs { h ^= hash(x); }
    return h;
}

// A function with a par for, called from a par for's body: it runs in place
// there, on the chunk's thread.
fn fill_row(row: i64[:], base: i64) {
    par for &c, j in row { c = base + j; }
}

fn insert(table: Slot[:], key: i64, val: i64) {
    var h = slot_of(key, table.len);
    while table[h].key != 0 && table[h].key != key { h = (h + 1) % table.len; }
    table[h] = Slot { key: key, val: val };
}

fn main() {
    // Filter.
    var vals: i64[>..] = [];
    for i in 1000 { vals.push((i * 7919) % 1009); }
    var keep: i64[>..] = [];
    keep.resize(vals.len, 0);
    var counts: i64[4] = [0, 0, 0, 0];
    filter(vals, keep, counts, 500);
    var kept = 0;
    var ksum = 0;
    for k in P {
        let run = keep.chunk(k, P);
        for j in counts[k] { ksum += run[j]; }
        kept += counts[k];
    }
    print("filter: ", counts, " kept ", kept, " sum ", ksum);

    // Probe.
    var table: Slot[>..] = [];
    table.resize(2048, Slot { key: 0, val: 0 });
    for i in 1..700 { insert(table, i * 3, i); }
    var out: i64[>..] = [];
    out.resize(vals.len, 0);
    var found: i64[4] = [0, 0, 0, 0];
    probe(vals, table, out, found);
    var osum = 0;
    for k in P {
        let run = out.chunk(k, P);
        for j in found[k] { osum += run[j]; }
    }
    print("probe: ", found, " sum ", osum);

    // Build.
    var rows: i64[>..] = [];
    for r in 1..201 { rows.push(r); }
    var parts: Slot[>..] = [];
    parts.resize(256, Slot { key: 0, val: 0 });
    let bounds = [0, 64, 128, 192, 256];
    build(rows, parts, bounds);
    var filled: i64[4] = [0, 0, 0, 0];
    var bsum = 0;
    for k in bounds.len - 1 {
        for s in parts.chunk_at(bounds, k) {
            if s.key != 0 {
                filled[k] += 1;
                bsum += s.val;
                assert(partition_of(s.key, 4) == k);
            }
        }
    }
    print("build: ", filled, " sum ", bsum);

    // Count distinct.
    var dup: i64[>..] = [];
    for i in 400 { dup.push(i % (40 + i / 100 * 10) + 1); }
    var distinct: i64[4] = [0, 0, 0, 0];
    count_distinct(dup, 8, distinct);
    print("distinct: ", distinct);

    // Totals.
    var a: f64[>..] = [];
    var b: f64[>..] = [];
    for i in 40 { a.push(i); b.push(0.5); }
    var partial: f64[4] = [0.0, 0.0, 0.0, 0.0];
    let nonzero = totals(a, b, partial);
    var sum = 0.0;
    for p in partial { sum += p; }
    print("totals: ", partial, " sum ", sum, " nonzero ", nonzero);

    // Blur.
    var img: u8[>..] = [];
    for y in H { for x in W { img.push(((x * 37 + y * 101) % 256) as! u8); } }
    var blurred: u8[>..] = [];
    blurred.resize(W * H, 0);
    blur(img, blurred);
    var bl = 0;
    for v in blurred { bl = bl * 31 % 1000003 + v; }
    var row: i64[>..] = [];
    for v in blurred.chunk(3, H) { row.push(v); }
    print("blur: ", row, " hash ", bl);

    // Every form.
    var xs: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    scale(xs, 3);
    print("scale: ", xs);
    print("checksum: ", checksum(xs));
    var total = 0;
    var neg = 0;
    var odd = 0;
    var any: u32 = 0;
    var all: u8  = 255;
    par for k in 1..101 {
        total += k;
        neg -= 2 * k;
        if k % 2 == 1 { odd++; }
        any |= (1 as u32) << (k % 20);
        if k % 3 == 0 { all &= ~((1 as u8) << (k % 5)); }
    }
    print("reductions: ", total, " ", neg, " ", odd, " ", any, " ", all);
    var at: i64[10] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    par for v, n in 10..20 { at[n] = v * 100 + n; }
    print("range: ", at);
    var grid: i64[16] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    par for i in 4 {
        let row = grid.chunk(i, 4);
        par for &c, j in row { c = i * 10 + j; }  // a par for in a par for: in place
    }
    print("nested: ", grid);
    var pairs = 0;
    par for i in 10 {
        par for j in 10 { if (i + j) % 3 == 0 { pairs++; } }   // a reduction of both
    }
    print("nested reduction: ", pairs);
    var rows2: i64[12] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    par for i in 3 { fill_row(rows2.chunk(i, 3), i * 100); }
    print("called: ", rows2);
    var marks: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
    par for k in 8 {
        defer { marks[k] += 1; }                // as each iteration ends
        marks[k] = k * 2;
    }
    print("defer: ", marks);
    var none = 0;
    par for k in 0 { none += 1; }
    par for k in 1 { none += 10; }
    print("small: ", none);
}
