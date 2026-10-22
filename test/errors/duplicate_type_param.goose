// error: duplicate type parameter name: T
// A declaration names each of its type parameters once (§3.2, §7.1): two
// would make S<i32, i64> a struct whose one T is both.
struct S<T, T> { a: T }
fn main() {
    let s = S<i32, i64> { a: 1 };
    print(s.a);
}
