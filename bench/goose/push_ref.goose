// `push`'s growth loop moved behind fat references: the same work, with the
// arrays reached through reference parameters instead of named locals. This
// is what a builder shared between several arrays looks like, and it is the
// shape data-stack top caching through a reference is for -- a push then
// bumps a register instead of reading and writing `p.stk->top` per element.
// Two callers so the builder stays a call rather than being inlined back into
// one of them.

import rng;

let N = 4000000;                         // BENCH_N

struct Item { key: i32, val: i32 }

fn fill(items: Item[>..]&, marks: Item&[>..]&, seed: u64, n: i64) {
    var r = seed;
    for i in n {
        r = xs_next(r);
        let e: Item& = items.push(Item { key: xs_mod(r, 1000000) as i32,
                                         val: (i % 97) as i32 });
        if i % 64 == 0 { marks.push(e); }    // A pointer, kept across all later growth.
    }
}

fn main() {
    var items: Item[>..] = [];
    var marks: Item&[>..] = [];
    var spare: Item[>..] = [];
    var spare_marks: Item&[>..] = [];
    fill(&items, &marks, 12345, N - N / 4);
    fill(&spare, &spare_marks, 6789, N / 4);
    var total = 0;
    for it in items { total += it.key; }
    for it in spare { total += it.key; }
    var mt = 0;
    for m in marks { mt += m.val; }
    for m in spare_marks { mt += m.val; }
    print(items.len + spare.len);
    print(marks.len + spare_marks.len);
    print(total);
    print(mt);
}
