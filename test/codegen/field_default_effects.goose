var count: i64 = 0;
let seed: i64 = 10;
fn tick() -> i64 { count++; count }
struct S<T> { first: i64 = tick(), middle: i64, last: i64 = seed }
struct D { n: i64 = tick() }
enum E { One { n: i64 = tick() }, Two }
fn main() {
    let seed = 99;
    fn tick() -> i64 { 999 }
    let a = S<u8> { middle: ::tick() };
    let b = S<u8> { middle: ::tick() };
    print(a, " ", b);
    let ds = default<D[2]>();
    let e = default<E>();
    print(ds, " ", e);
    reusable[] var pool: D[>..] = [];
    let run = pool.alloc_slice(2);
    print(run, " ", count);
}
