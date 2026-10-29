// runtime-debug: each new value gets one default, including a moved run's suffix.
import slice_pool;
var calls = 0;
fn next() -> i64 { calls++; calls }
struct Stamp { value: i64 = next() }
fn main() {
    var pool = slice_pool<Stamp> {};
    let a = pool.alloc_slice(3);
    assert(calls == 3 && a[2].value == 3);
    let blocker = pool.alloc_slice(2);
    let b = pool.realloc_slice(a, 5);
    assert(calls == 7 && b[0].value == 1 && b[2].value == 3 && b[4].value == 7);
    pool.free_slice(blocker);
    let c = pool.realloc_slice(b, 7);
    assert(calls == 9 && c[0].value == 1 && c[6].value == 9);
    let d = pool.alloc_slice(4);
    assert(calls == 13 && d[0].value == 10 && d[3].value == 13);
    let e = pool.realloc_slice(d, 1);
    assert(calls == 13 && e[0].value == 10);
    print("slice defaults");
}
