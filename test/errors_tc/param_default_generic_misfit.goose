// error: the default of parameter b cannot be passed as u8: constant 300 does not fit u8
// The default adapts to the type the arguments give the parameter: an i64
// takes it, a u8 does not (§7.1).
fn f<T>(a: T, b: T = 300) -> T { a + b }
fn main() {
    print(f(1));
    let x: u8 = 1;
    print(f(x));
}
