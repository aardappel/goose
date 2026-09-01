// Growth while holding element pointers: see bench/goose/push.goose. Safe Rust
// has no answer to this at all -- a `&Item` into `items` borrows the vector for
// as long as it is held, so the next `push` will not compile, and no std
// container gives both pointer stability and O(1) append. The marks are
// therefore indices, which is not a performance choice but the only shape the
// language admits. The capacity is reserved because N is a known constant here
// and a Rust programmer would write that; it keeps the benchmark about the
// pointers-versus-indices question rather than about reallocation.
mod bench;
use bench::*;

const N: i64 = 1000000;                  // BENCH_N

struct Item { key: i32, val: i32 }

fn main() {
    let mut items: Vec<Item> = Vec::with_capacity(N as usize);
    let mut marks: Vec<usize> = Vec::new();
    let mut r: u64 = 12345;
    for i in 0..N {
        r = xs_next(r);
        items.push(Item { key: xs_mod(r, 1000000) as i32, val: (i % 97) as i32 });
        if i % 64 == 0 { marks.push(items.len() - 1); }
    }
    let mut total: i64 = 0;
    for it in &items { total += it.key as i64; }
    let mut mt: i64 = 0;
    for &m in &marks { mt += items[m].val as i64; }
    emit(items.len() as i64);
    emit(marks.len() as i64);
    emit(total);
    emit(mt);
}
