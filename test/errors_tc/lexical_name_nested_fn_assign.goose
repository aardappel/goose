// error: K names a nested function, not a variable
// The nested function K hides the global variable K (§11.1).
var K = 3;

fn main() {
    fn K(x: i64) -> i64 { x }
    K = 4;
    print(K(1));
}
