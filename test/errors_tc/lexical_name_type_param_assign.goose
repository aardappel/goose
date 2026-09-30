// error: N names a type parameter, not a variable
// The type parameter hides the global variable N (§11.1).
var N = 4;

fn f<N>(x: N) -> i64 {
    N = 5;
    0
}

fn main() { print(f(1), " ", N); }
