// error: array size K names a local variable, not a constant
// A nested function's body names what is in scope at its declaration
// (§7.5): main's K, which hides the global.
let K = 3;

fn main() {
    let K = 5;
    fn g() -> i64 {
        let a: i64[K] = [1, 2, 3];
        a.len
    }
    print(g(), " ", K);
}
