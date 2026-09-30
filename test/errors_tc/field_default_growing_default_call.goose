// error: the default of field n of W<i64> leads to this construction of W<W<i64>>, which takes it again
// default<T>() of an ever larger instantiation (§3.2, §4.2).
struct W<T> { v: T? = null, n: i64 = default<W<W<T>>>().n }
fn main() {
    let x = default<W<i64>>();
    print(x.n);
}
