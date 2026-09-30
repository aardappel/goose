// error: instantiating A<i64[][][][][][][][][][][][][][][][]> would chain more than 16 instantiations of A, each needed by the one before (A<i64>, A<i64[]>, A<i64[][]>, ...)
// The same through a second declaration: A<T> names B<T[]>, which names
// A<T> with what it got, so each round of the pair adds a level (§3.2).
struct A<T> { v: T, b: B<T[]>? }
struct B<T> { a: A<T>? }
fn main() {
    let a = A<i64> { v: 1, b: null };
    print(a.v);
}
