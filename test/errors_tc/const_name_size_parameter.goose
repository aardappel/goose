// error: array size K names a local variable, not a constant
// A parameter is a local of its function: it hides the global K in the body.
let K = 3;

fn f(K: i64) -> i64 {
    var a: i64[K] = [0; 3];
    a.len
}

fn main() { print(f(5)); }
