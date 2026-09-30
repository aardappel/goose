// error: array size N names a type parameter, not a constant
// A generic struct's fields name its type parameters, which hide the global
// N and stand for types (§3.3).
let N = 4;

struct S<N> { a: i64[N], b: N }

fn main() {
    let s = S<u8> { a: [1, 2, 3, 4], b: 1 };
    print(s.a.len);
}
