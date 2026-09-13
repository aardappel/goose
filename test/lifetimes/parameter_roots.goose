// runtime-debug: holder arguments must not shift a later reusable pool's facts.
struct Holder { value: i64& }

fn recycle(h: Holder, pool: i64[>..]&) -> i64 {
    let i = pool.alloc_index(h.value);
    pool.free(i);
    pool.alloc_index(27)
}

fn main() {
    var value = 11;
    let h = Holder { value: value };
    reusable var pool: i64[>..] = [];
    print(recycle(h, pool));
    print(pool.len);
    print(pool[0]);
}
