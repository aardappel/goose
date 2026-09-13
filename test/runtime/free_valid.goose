// runtime-debug: valid reusable-pool boundaries and one evaluation of the index.
var calls = 0;

fn last_slot() -> i64 { calls++; return 1; }

fn main() {
    reusable var pool: i64[>..] = [11, 22];
    pool.free(last_slot());
    let last = pool.alloc_index(33);
    print(last, " ", calls, " ", pool[last], " ", pool.len);
    pool.free(0);
    let first: i64& = pool.alloc_ref(44);
    print(pool.index_of(first), " ", first, " ", pool.len);
}
