// error: instantiating P<i64[][][][][][][][][][][][][][][][]> would chain more than 16 instantiations of P, each needed by the one before (P<i64>, P<i64[]>, P<i64[][]>, ...)
// error: a generic type must reach a finite set of instantiations
// Every instantiation of a struct checks its fields' types, so a field
// naming the struct again with a larger type argument needs P<i64[]>, then
// P<i64[][]>, without end, even though no value of any of them is ever
// made (§3.2).
struct P<T> { v: T, next: P<T[]>? }
fn main() {
    let p = P<i64> { v: 1, next: null };
    print(p.v);
    print(p);
}
