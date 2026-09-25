// error: .= binds a reference: the initializer must be a reference, a slice, or storage (a variable, field or element)
// A function value's call gives a new value, not the storage its body's
// tail named.
var g: i64[3] = [1, 2, 3];
fn poke<F>() -> i64 {
    let r .= F();
    r[0] = 7;
    g[0]
}
fn main() { print(poke() { g }); }
