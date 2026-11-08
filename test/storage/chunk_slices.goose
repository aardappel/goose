// a.chunk(k, n) and a.chunk_at(bounds, k) outside a parallel loop, where
// they are ordinary slicing (docs/design/child_threads.md §4.4). chunk is
// the k-th of n contiguous runs that cover a, their lengths differing by at
// most one, the longer ones first; chunk_at is a[bounds[k]..bounds[k + 1]].
// Either is the slice a[lo..hi] would be: writable where a is, `const`
// where it is read-only, rooted where a's elements lie.

struct Point { x: i32, y: i32 }
struct Grid { cells: i64[6] }

// Every chunk of xs, in order: each starts where the one before it ended,
// and together they are xs.
fn runs(xs: const i64[:], n: i64) {
    var line: u8[>..] = [];
    var covered = 0;
    for k in n {
        let c = xs.chunk(k, n);
        assert(c == xs[covered..covered + c.len]);
        covered += c.len;
        format(line, " ", c);
    }
    assert(covered == xs.len);
    print(xs.len, " in ", n, ":", line);
}

// A chunk returned is rooted where xs is, as xs[lo..hi] would be.
fn half(xs: i64[:], k: i64) -> i64[:] { xs.chunk(k, 2) }

fn main() {
    var a: i64[>..] = [];
    for i in 12 { a.push(i); }
    runs(a, 3);                     // a length n divides
    runs(a[..10], 4);               // one it does not: the longer runs first
    runs(a[..3], 5);                // shorter than n: empty runs at the end
    runs(a[..0], 3);                // empty
    runs(a, 1);
    let n32: i32 = 4;
    print(a.chunk(3, n32), " ", chunk(a, 0, 5).len, " ", a.chunk(4, 5));

    // Writable storage gives writable chunks, written in place.
    var w: i64[10] = [0; 10];
    for k in 4 {
        for &x in w.chunk(k, 4) { x = k + 1; }
    }
    print(w);
    let mid = chunk(w, 1, 2);
    mid[0] = 99;
    w.chunk(2, 3)[1] = 77;
    print(w, " ", mid);

    // chunk_at: the bounds as a fixed array, a slice of one and a grow-only
    // array; equal bounds give an empty chunk.
    let bounds: i64[5] = [0, 2, 2, 5, 9];
    var b: i64[>..<] = [];
    for i in 9 { b.push(i * 10); }
    for k in 4 { print("chunk_at ", k, ": ", b.chunk_at(bounds, k)); }
    for k in 4 {
        for &x in b.chunk_at(bounds[..], k) { x += k; }
    }
    var gb: i64[>..] = [3, 7];
    chunk_at(b, gb, 0)[0] = -1;
    print(b, " ", b.chunk_at(gb, 0).len);

    // Read-only storage gives `const` chunks, which read the same.
    const fixed: i64[6] = [1, 2, 3, 4, 5, 6];
    let cf = fixed.chunk(1, 2);
    let view: const i64[:] = a;
    print(cf, " ", fixed.chunk_at(bounds, 2), " ", view.chunk(2, 3), " ",
          "hello world".chunk(1, 2), " ", "abc".chunk_at(bounds, 0));

    // Other element types and receivers: a struct's field, a limited array,
    // a chunk of a chunk, and one returned from a function.
    var pts: Point[>..] = [];
    for i: i32 in 5 { pts.push(Point { i, -i }); }
    for &p in pts.chunk(1, 2) { p.x *= 10; }
    var g = Grid { cells: [1, 2, 3, 4, 5, 6] };
    for &x in g.cells.chunk(2, 3) { x = 0; }
    var lim: u8[..16] = "abcdefg";
    for &ch in lim.chunk(0, 2) { ch -= 32; }
    print(pts, " ", g.cells, " ", lim);
    let h = half(a, 1);
    h.chunk(0, 3)[0] = -6;
    print(h, " ", h.chunk(2, 3), " ", a.chunk(1, 2).chunk(1, 2), " ", a[6]);

    // A chunk of a grow-only array keeps its elements as the array grows,
    // and a later chunk sees the new length.
    let first = a.chunk(0, 4);
    a.push(12);
    print(first, " ", a.chunk(0, 4), " ", a.chunk(3, 4));
}
