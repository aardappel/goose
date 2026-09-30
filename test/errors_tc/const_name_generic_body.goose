// error: array size K names a local variable, not a constant
// A generic body's sizes resolve in its scope whatever T is bound to.
let K = 3;

fn f<T>(x: T) -> i64 {
    let K = 5;
    var a: T[K] = [x; 3];
    a.len
}

fn main() { print(f(1)); }
