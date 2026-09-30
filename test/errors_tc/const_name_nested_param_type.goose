// error: array size K names a local variable, not a constant
// A nested function's signature is written in the scope it is declared in,
// where K is main's local (§3.3, §7.5).
let K = 3;

fn main() {
    let K = 5;
    fn g(a: i64[K]) -> i64 { a.len }
    print(g([1, 2, 3]), " ", K);
}
