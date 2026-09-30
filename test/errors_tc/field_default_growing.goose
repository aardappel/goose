// error: the default of field d of D<i64> leads to this construction of D<i64[]>, which takes it again
// Each round is a new instantiation, and still the same default of the same
// declaration (§3.2).
struct D<T> { v: T[] = [], d: i64 = D<T[]> { .. }.d }
fn main() {
    let x = D<i64> { .. };
    print(x.d);
}
