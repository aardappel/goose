// runtime-debug: a slice pool with thousands of free spans at once: first fit, merging and growth stay exact (§5.4).
// Lists grown an element at a time in a scattered order leave a free span
// behind most moves; freeing whole lists and parts of lists, and placing
// new runs, then works through many free spans at once. Every list keeps its
// own elements, the placements add up to the same checksum, and once every
// run is freed the pool is one free span again.

var seed: u64 = 12345;

fn next(n: i64) -> i64 {
    seed = seed * 6364136223846793005 + 1442695040888963407;
    return ((seed >> 33) % n as! u64) as! i64;
}

fn main() {
    reusable[] var pool: i64[>..] = [];
    let n = 3000;
    var lists: (i64[:])[>..] = [];
    for i in n { lists.push(pool.alloc_slice(0)); }

    // Each element holds its list's number and its place in the list.
    for step in n * 20 {
        let i = next(n);
        let len = lists[i].len;
        lists[i] = pool.realloc_slice(lists[i], len + 1);
        lists[i][len] = i * 1000 + len;
    }
    var spans = 0;
    for i in n { if lists[i].len > 0 { spans += 1; } }
    print("grown: pool ", pool.len, ", lists holding elements ", spans);

    // Every third list goes back whole, the front half of every fifth and
    // the back of every seventh.
    for i in n {
        let s = lists[i];
        if i % 3 == 0 {
            pool.free_slice(s);
            lists[i] = s[..0];
        } else if i % 5 == 0 && s.len >= 2 {
            pool.free_slice(s[..s.len / 2]);
            lists[i] = s[s.len / 2..];
        } else if i % 7 == 0 && s.len >= 2 {
            lists[i] = pool.realloc_slice(s, s.len - s.len / 3);
        }
    }

    // New runs of every size from 1 to 40 land in the first span that holds
    // them, and some lists grow again.
    var placed: i64 = 0;
    var extra: (i64[:])[>..] = [];
    for k in 4000 {
        let s = pool.alloc_slice(1 + next(40));
        for j in s.len { s[j] = -1; }
        placed = (placed * 31 + pool.index_of(s[0])) % 1000000007;
        extra.push(s);
        let i = next(n);
        if i % 3 != 0 {
            let len = lists[i].len;
            let first = if len > 0 { lists[i][0] % 1000 } else { 0 };
            lists[i] = pool.realloc_slice(lists[i], len + 1);
            lists[i][len] = i * 1000 + first + len;
        }
    }
    print("placed: pool ", pool.len, ", checksum ", placed);

    // Every list still holds exactly its own elements, in order.
    var bad = 0;
    for i in n {
        let s = lists[i];
        for j in s.len {
            if s[j] / 1000 != i || (j > 0 && s[j] != s[j - 1] + 1) { bad++; }
        }
    }
    for s in extra { for v in s { if v != -1 { bad++; } } }
    print("elements out of place: ", bad);

    // All of it back, in a scattered order: the freed runs merge into one span.
    for i in n {
        let j = (i * 1777) % n;
        pool.free_slice(lists[j]);
    }
    for s in extra { pool.free_slice(s); }
    let all = pool.alloc_slice(pool.len);
    print("one span: ", pool.index_of(all[0]), " ", all.len == pool.len);
}
