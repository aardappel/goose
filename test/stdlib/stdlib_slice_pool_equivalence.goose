// runtime-debug all-levels: paired traces check placement, contents, shrinking and retained views.
import slice_pool;
fn next(r: u64) -> u64 { r * 6364136223846793005 + 1442695040888963407 }
fn main() {
    var pool = slice_pool<i64> {};
    reusable[] var builtin: i64[>..] = [];
    let kept = pool.alloc_slice(1);
    let bkept = builtin.alloc_slice(1);
    kept[0] = -1; bkept[0] = -1;
    var runs: (i64[:])[>..] = [];
    var bruns: (i64[:])[>..] = [];
    for i in 128 { runs.push(pool.alloc_slice(0)); bruns.push(builtin.alloc_slice(0)); }
    var random: u64 = 98765;
    for step in 4000 {
        random = next(random);
        let i = ((random >> 32) % 128) as i64;
        let n = ((random >> 16) % 65) as i64;
        if random % 4 == 0 {
            pool.free_slice(runs[i]); builtin.free_slice(bruns[i]);
            runs[i] = pool.alloc_slice(n); bruns[i] = builtin.alloc_slice(n);
        } else {
            runs[i] = pool.realloc_slice(runs[i], n);
            bruns[i] = builtin.realloc_slice(bruns[i], n);
        }
        assert(runs[i] == bruns[i]);
        assert(pool.items.range_index(runs[i]) == builtin.range_index(bruns[i]));
        for j in n { runs[i][j] = step * 100 + j; bruns[i][j] = step * 100 + j; }
        assert(pool.items == builtin && kept[0] == bkept[0]);
    }
    for i in runs.len { pool.free_slice(runs[i]); builtin.free_slice(bruns[i]); }
    let all = pool.alloc_slice(pool.items.len - 1);
    let ball = builtin.alloc_slice(builtin.len - 1);
    assert(pool.items.range_index(all) == builtin.range_index(ball));
    assert(pool.items == builtin && kept[0] == -1);
    print("slice traces agree");
}
