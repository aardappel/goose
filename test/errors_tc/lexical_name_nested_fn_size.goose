// error: array size K names a nested function, not a constant
// The nested function K hides the global K (§3.3, §11.1).
let K = 3;

fn main() {
    fn K(x: i64) -> i64 { x }
    var a: i64[K] = [0, 0, 0];
    print(a.len, " ", K(1));
}
