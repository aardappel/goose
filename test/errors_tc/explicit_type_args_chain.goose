// error: constant 300 does not fit u8
// Both calls in main pass an f64 but differ in an explicit type argument no
// parameter type names, so they are separate instantiations (§7.7) that the
// argument types alone would print alike. The chain lists such type
// arguments: this error is in `cap<T = u8>()`, reached from
// `scale<U = u8>(f64)`.
fn cap<T>() -> T { let x: T = 300; x }
fn scale<T, U>(t: T) -> T { if cap<U>() > 0 { t } else { -t } }
fn main() {
    let x = 1.5;
    print(scale<f64, i64>(x), " ", scale<f64, u8>(x));
}
