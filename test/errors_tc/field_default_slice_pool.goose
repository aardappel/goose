// error: the default of field d of S leads to this construction of S, which takes it again
// A slice pool fills what it allocates with default values (§5.4).
reusable[] var pool: S[>..] = [];
struct S { a: i64 = 1, d: i64 = pool.alloc_slice(1).len }
fn main() {
    let x = S { .. };
    print(x.d);
}
