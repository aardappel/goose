// bench/slice_pool.py derives reusable[] from the identical workload.
import slice_pool;
import rng;
let N = 1000000;
let LIVE = 4096;
let WIDTH = 1;
let MODE = 0; // 0 append, 1 fragmented free/allocate, 2 resize/move.
struct Item { words: i64[WIDTH] }

fn exercise(p: slice_pool<Item>&, n: i64, seed: u64) -> u64 {
    let kept = p.alloc_slice(1);
    kept[0].words[0] = seed as i64;
    var checksum: u64 = 0;
    if MODE == 0 {
        for i in n {
            let s = p.alloc_slice(16);
            s[0].words[0] = i;
            s[15].words[WIDTH - 1] = i + 1;
            checksum += p.items.range_index(s) as u64;
        }
    } else {
        var runs: (Item[:])[>..] = [];
        for i in LIVE {
            let s = p.alloc_slice(8 + i % 9);
            s[0].words[0] = i;
            runs.push(s);
            // Live separators keep freed spans distinct and exercise tree splits.
            let separator = p.alloc_slice(1);
            separator[0].words[0] = -1;
        }
        var random = seed;
        for step in n {
            random = xs_next(random);
            let i = xs_mod(random, LIVE);
            let old = runs[i];
            if MODE == 1 {
                p.free_slice(old);
                runs[i] = p.alloc_slice(1 + ((random >> 20) % 64) as i64);
            } else {
                let size = if old.len >= 64 { 1 } else { old.len + 1 };
                runs[i] = p.realloc_slice(old, size);
                assert(runs[i][0].words[0] == i);
            }
            runs[i][0].words[0] = i;
            checksum += p.items.range_index(runs[i]) as u64;
        }
        for s in runs { for x in s { for word in x.words { checksum += word as! u64; } } }
    }
    assert(kept[0].words[0] == seed as i64);
    return checksum;
}
fn main() {
    var a = slice_pool<Item> {};
    var b = slice_pool<Item> {};
    let checksum = exercise(a, N - N / 4, 12345) + exercise(b, N / 4, 6789);
    var values: u64 = 0;
    for x in a.items { for word in x.words { values += word as! u64; } }
    for x in b.items { for word in x.words { values += word as! u64; } }
    print(checksum, " ", values, " ", a.items.len + b.items.len);
}
