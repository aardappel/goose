// The same algorithm and workload as push_ref, with one root owner per
// items/marks pair. Compare at Goose -O0 as well as -O2: -O0 keeps the
// descriptor boundary while the native compiler optimizes the C loops.
import rng;

let N = 4000000;                         // BENCH_N
struct Item { key: i32, val: i32 }
root struct State { items: Item[>..], marks: Item&[>..] }

fn fill(s: State&, seed: u64, n: i64) {
    var r = seed;
    for i in n {
        r = xs_next(r);
        let e: Item& = s.items.push(Item { key: xs_mod(r, 1000000) as i32,
                                         val: (i % 97) as i32 });
        if i % 64 == 0 { s.marks.push(e); }
    }
}

fn main() {
    var s = State { .. };
    var spare = State { .. };
    fill(s, 12345, N - N / 4);
    fill(spare, 6789, N / 4);
    var total = 0;
    for it in s.items { total += it.key; }
    for it in spare.items { total += it.key; }
    var mt = 0;
    for m in s.marks { mt += m.val; }
    for m in spare.marks { mt += m.val; }
    print(s.items.len + spare.items.len);
    print(s.marks.len + spare.marks.len);
    print(total);
    print(mt);
}
