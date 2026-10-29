// bench/slot_pool.py derives the reusable version from this same workload.
// MODE: 0 append-only, 1 sequential batches of reuse, 2 scattered batches.
import slot_pool;
import rng;

let N = 4000000;                         // BENCH_N
let LIVE = 4096;                         // BENCH_LIVE, power of two >= 64
let WIDTH = 1;                           // BENCH_WIDTH, 8 or 128 byte items
let MODE = 0;                            // BENCH_MODE
struct Item { words: i64[WIDTH] }

fn exercise(p: slot_pool<Item>&, n: i64, seed: u64) -> u64 {
    let kept .= p.alloc_ref(Item { [seed as i64; WIDTH] });
    var checksum: u64 = 0;
    if MODE == 0 {
        for i in n {
            let value = (seed + i as u64) as i64;
            let r .= p.alloc_ref(Item { [value; WIDTH] });
            checksum += p.items.index_of(r) as u64 * (i + 1) as u64;
        }
    } else {
        for i in LIVE { p.alloc_index(Item { [i; WIDTH] }); }
        var random = seed;
        for batch in n / 64 {
            random = xs_next(random);
            let start = if MODE == 2 { xs_mod(random, LIVE) } else { batch * 64 };
            let stride = if MODE == 2 { 17 } else { 1 };
            // A unique set of 64 live slots; slot zero is never freed.
            for k in 64 { p.free(1 + ((start + k * stride) & (LIVE - 1))); }
            for k in 64 {
                let i = batch * 64 + k;
                let index = p.alloc_index(Item { [(seed + i as u64) as i64; WIDTH] });
                checksum += index as u64 * (i + 1) as u64;
            }
        }
    }
    assert(kept.words[0] == seed as i64);
    return checksum;
}

fn main() {
    var a = slot_pool<Item> {};
    var b = slot_pool<Item> {};
    let checksum = exercise(a, N - N / 4, 12345) + exercise(b, N / 4, 6789);
    var values: u64 = 0;
    for item in a.items { for word in item.words { values += word as u64; } }
    for item in b.items { for word in item.words { values += word as u64; } }
    print(checksum, " ", values, " ", a.items.len + b.items.len);
}
