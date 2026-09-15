// runtime-debug: reusable[] slice pools: first fit, merging, growth in place and by moving (§5.4).
struct Cell { v: i64 = -1, w: i32 = 5 }

fn at(pool: i64[>..]&, s: i64[:]) -> i64 { pool.index_of(s[0]) }

fn fill(s: i64[:], from: i64) {
    for i in s.len { s[i] = from + i; }
}

// The first free span that holds a request serves it, from its front, even
// where a later one would fit exactly.
fn first_fit() {
    reusable[] var pool: i64[>..] = [];
    let a = pool.alloc_slice(4);        // [0, 4)
    let b = pool.alloc_slice(1);        // [4, 5)
    let c = pool.alloc_slice(2);        // [5, 7)
    let d = pool.alloc_slice(1);        // [7, 8)
    let e = pool.alloc_slice(3);        // [8, 11)
    let f = pool.alloc_slice(2);        // [11, 13)
    let g = pool.alloc_slice(1);        // [13, 14): no free span reaches the end
    pool.free_slice(a);
    pool.free_slice(c);
    pool.free_slice(e);
    pool.free_slice(f);                 // (0, 4) (5, 2) (8, 5)
    let h = pool.alloc_slice(2);        // (0, 4), not the exact (5, 2): leaves (2, 2)
    let k = pool.alloc_slice(3);        // (8, 5), leaving (11, 2)
    let m = pool.alloc_slice(5);        // nothing holds 5: the end
    let n = pool.alloc_slice(1);        // (2, 2), leaving (3, 1)
    let o = pool.alloc_slice(2);        // (5, 2)
    print(at(pool, h), " ", at(pool, k), " ", at(pool, m), " ", at(pool, n), " ",
          at(pool, o), " ", pool.len, " ", b.len + d.len + g.len);   // 0 8 14 2 5 19 3
}

// A freed slice merges with the free spans it touches, on either side.
fn merging() {
    reusable[] var pool: i64[>..] = [];
    let s0 = pool.alloc_slice(2);       // [0, 2)
    let s1 = pool.alloc_slice(2);       // [2, 4)
    let s2 = pool.alloc_slice(2);       // [4, 6)
    let s3 = pool.alloc_slice(2);       // [6, 8)
    let s4 = pool.alloc_slice(2);       // [8, 10)
    let s5 = pool.alloc_slice(1);       // [10, 11)
    pool.free_slice(s0);                // (0, 2)
    pool.free_slice(s1);                // after the span before it: (0, 4)
    pool.free_slice(s4);                // (0, 4) (8, 2)
    pool.free_slice(s3);                // before the span after it: (0, 4) (6, 4)
    pool.free_slice(s2);                // between the two: (0, 10)
    let all = pool.alloc_slice(10);
    print(at(pool, all), " ", pool.len, " ", s5.len);   // 0 11 1
}

// A free span at the end of the array counts toward what an allocation
// adds there; the elements it reuses are default values again, and a slice
// still naming them sees whatever the new owner writes.
fn tail_and_dangling() {
    reusable[] var pool: i64[>..] = [];
    let a = pool.alloc_slice(3);        // [0, 3)
    let b = pool.alloc_slice(4);        // [3, 7)
    fill(a, 1);
    fill(b, 100);
    pool.free_slice(b);                 // (3, 4) reaches the end
    let c = pool.alloc_slice(6);        // no span holds 6: that one and 2 more
    print(at(pool, c), " ", pool.len, " ", c, " ", b);   // 3 9 [0, 0, 0, 0, 0, 0] [0, 0, 0, 0]
    c[1] = 42;
    print(b[1], " ", a);                // 42 [1, 2, 3]

    reusable[] var cells: Cell[>..] = [];
    let x = cells.alloc_slice(2);
    x[1].v = 9;
    cells.free_slice(x);
    let y = cells.alloc_slice(2);       // the same elements, defaults again
    print(x[1].v, " ", x[1].w, " ", cells.index_of(y[0]));   // -1 5 0
    y[1].w = 3;
    print(x[1]);                        // Cell { -1, 3 }
}

// realloc_slice grows in place where the slice ends the array or free
// elements follow it, and otherwise moves it where alloc_slice would put
// it; shrinking frees the rest.
fn growth() {
    reusable[] var pool: i64[>..] = [];
    var a = pool.alloc_slice(2);        // [0, 2)
    fill(a, 1);
    a = pool.realloc_slice(a, 4);       // last in the array
    print(at(pool, a), " ", a, " ", pool.len);   // 0 [1, 2, 0, 0] 4
    var b = pool.alloc_slice(3);        // [4, 7)
    fill(b, 10);
    let c = pool.alloc_slice(2);        // [7, 9)
    fill(c, 20);
    pool.free_slice(c);                 // (7, 2)
    b = pool.realloc_slice(b, 4);       // into the free span after it, leaving (8, 1)
    print(at(pool, b), " ", b, " ", pool.len);   // 4 [10, 11, 12, 0] 9
    b = pool.realloc_slice(b, 7);       // (8, 1) ends the array: it and 2 more
    print(at(pool, b), " ", b, " ", pool.len);   // 4 [10, 11, 12, 0, 0, 0, 0] 11
    a = pool.realloc_slice(a, 5);       // b follows a, and nothing free holds 5: the end
    print(at(pool, a), " ", a, " ", pool.len);   // 11 [1, 2, 0, 0, 0] 16
    let d = pool.alloc_slice(4);        // where a was
    print(at(pool, d), " ", d, " ", pool.len);   // 0 [0, 0, 0, 0] 16
    b = pool.realloc_slice(b, 2);       // frees (6, 5)
    let e = pool.alloc_slice(5);
    print(at(pool, b), " ", b, " ", at(pool, e), " ", pool.len);   // 4 [10, 11] 6 16
    let none = pool.alloc_slice(0);     // takes nothing
    pool.free_slice(none);              // and gives nothing back
    let gone = pool.realloc_slice(e, 0);   // all of e back: (6, 5)
    let f = pool.alloc_slice(5);
    print(none.len, " ", gone.len, " ", at(pool, f), " ", pool.len);   // 0 0 6 16
    let tailing = pool.realloc_slice(none, 2);   // empty: placed like a new run, at the end
    print(at(pool, tailing), " ", pool.len);     // 16 18
    // A slice moved past a free span at the end absorbs it.
    let g = pool.alloc_slice(3);        // [18, 21)
    let h = pool.alloc_slice(3);        // [21, 24)
    pool.free_slice(h);                 // (21, 3) reaches the end
    let moved = pool.realloc_slice(d, 5);   // d is followed by b: moved into (21, 3) and 2 more
    print(at(pool, moved), " ", pool.len, " ", g.len);   // 21 26 3
}

// A slice that cannot grow in place moves where alloc_slice would put the
// grown run, its own place freed first: to the first span that holds it,
// which can be that place merged with the free elements beside it.
fn moves() {
    reusable[] var pool: i64[>..] = [];
    let a = pool.alloc_slice(3);        // [0, 3)
    let b = pool.alloc_slice(1);        // [3, 4)
    let c = pool.alloc_slice(8);        // [4, 12)
    let d = pool.alloc_slice(1);        // [12, 13)
    let e = pool.alloc_slice(2);        // [13, 15)
    let f = pool.alloc_slice(1);        // [15, 16)
    fill(e, 40);
    pool.free_slice(a);
    pool.free_slice(c);                 // (0, 3) (4, 8)
    let e2 = pool.realloc_slice(e, 6);  // f follows e: into (4, 8), not the end
    print(at(pool, e2), " ", e2, " ", pool.len, " ", b.len + d.len + f.len);   // 4 [40, 41, 0, 0, 0, 0] 16 3

    reusable[] var p2: i64[>..] = [];
    let x = p2.alloc_slice(2);          // [0, 2)
    let y = p2.alloc_slice(4);          // [2, 6)
    let z = p2.alloc_slice(1);          // [6, 7)
    fill(y, 60);
    p2.free_slice(x);                   // (0, 2)
    let y2 = p2.realloc_slice(y, 6);    // z follows y: y's place and (0, 2) hold it
    print(at(p2, y2), " ", y2, " ", p2.len, " ", z.len);   // 0 [60, 61, 62, 63, 0, 0] 7 1
}

// Through a reference, into a nested function and down a recursion, the pool
// travels with its freelist.
fn extend_by(pool: i64[>..]&, s: i64[:], k: i64) -> i64 {
    let t = pool.realloc_slice(s, s.len + k);
    fill(t, 1);
    var sum = 0;
    for i in t.len { sum += t[i]; }
    return sum;
}

recursive fn churn(pool: i64[>..]&, n: i64) -> i64 {
    if n == 0 { return pool.len; }
    let s = pool.alloc_slice(n);
    let r = churn(pool, n - 1);
    pool.free_slice(s);
    return r;
}

reusable[] var gpool: Cell[>..] = [];

fn through() {
    reusable[] var pool: i64[>..] = [];
    let s = pool.alloc_slice(2);
    print(extend_by(pool, s, 3), " ", pool.len);   // 15 5
    fn grab(n: i64) -> i64 {
        let t = pool.alloc_slice(n);
        return pool.index_of(t[0]);
    }
    pool.free_slice(s);                            // (0, 2): short of 3, and not at the end
    print(grab(3), " ", grab(1), " ", pool.len);   // 5 0 8
    // The first run grows the array to 17, and the second finds all it
    // needs in what the first freed.
    print(churn(pool, 4), " ", churn(pool, 4));    // 17 17
    let ss = gpool.alloc_slice(3);
    gpool.free_slice(ss[1..]);
    let tt = gpool.alloc_slice(2);
    print(gpool.index_of(tt[0]), " ", gpool.len, " ", tt[0]);   // 1 3 Cell { -1, 5 }
    let lim: i64[..4] = pool.alloc_slice(3);
    print(lim.len, " ", lim.cap, " ", lim);        // 3 4 [0, 0, 0]
}

// `in pool` links measure from the pool's base, so they survive a move.
struct Link { v: i64 = 0, next: Link&<u32 in lpool>? }
reusable[] var lpool: Link[>..] = [];

fn links() {
    var s = lpool.alloc_slice(2);
    s[0].v = 1;
    s[1].v = 2;
    s[0].next .= s[1];
    let blocker = lpool.alloc_slice(1);
    s = lpool.realloc_slice(s, 3);      // moved to [3, 6)
    let n = s[0].next;
    if n { print(n.v, " ", lpool.index_of(n), " ", lpool.index_of(s[0]), " ", blocker.len); }   // 2 1 3 1
}

// A run of `in pool` links kept in a global value reads back as a slice of
// the pool of runs, which is all realloc_slice and free_slice ask of it.
struct Item { v: i64 = 0 }
struct Group { members: Item&<u32 in items>?[:] }
var items: Item[>..] = [];
reusable[] var memberruns: Item&<u32 in items>?[>..] = [];
var groups: Group[>..] = [];

fn link_runs() {
    items.push(Item { v: 1 });
    items.push(Item { v: 2 });
    groups.push(Group { members: memberruns.alloc_slice(1) });
    groups[0].members[0] .= items[1];
    groups[0].members = memberruns.realloc_slice(groups[0].members, 2);
    groups[0].members[1] .= items[0];
    let a = groups[0].members[0];
    let b = groups[0].members[1];
    if a && b { print(a.v, " ", b.v, " ", memberruns.len); }   // 2 1 2
    memberruns.free_slice(groups[0].members);
    print(memberruns.alloc_slice(2).len, " ", memberruns.len);   // 2 2: the freed run
}

// A default slice points at no storage, and being empty it needs no position:
// given back it frees nothing, and grown it is placed like a new run.
struct Tally { counts: u16[:] }
reusable[] var countruns: u16[>..] = [];
var tallies: Tally[>..] = [];

fn default_runs() {
    countruns.alloc_slice(3);
    tallies.push(Tally { counts: default<u16[:]>() });
    tallies.push(Tally { counts: default<u16[:]>() });
    countruns.free_slice(tallies[0].counts);
    tallies[1].counts = countruns.realloc_slice(tallies[1].counts, 2);
    print(tallies[1].counts, " ", countruns.len);   // [0, 0] 5
}

// A run the checker cannot place in its pool -- read out of a descriptor,
// where `lines` could hold its bytes as well -- is checked to be the pool's
// when the call runs.
struct Doc { text: u8[:] }
reusable[] var textbytes: u8[>..] = [];
var lines: u8[>..] = [];
var documents: Doc[>..] = [];

fn text_runs() {
    lines.push('x');
    documents.push(Doc { text: textbytes.alloc_slice(3) });
    documents[0].text[0] = 'a';
    documents[0].text = textbytes.realloc_slice(documents[0].text, 5);
    print(documents[0].text.len, " ", textbytes.len, " ", documents[0].text[0]);   // 5 5 97
    textbytes.free_slice(documents[0].text);
    print(textbytes.alloc_slice(4).len, " ", textbytes.len);   // 4 5
}

fn main() {
    first_fit();
    merging();
    tail_and_dangling();
    growth();
    moves();
    through();
    links();
    link_runs();
    default_runs();
    text_runs();
}
