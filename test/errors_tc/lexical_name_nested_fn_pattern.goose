// error: match pattern K names a nested function, not a constant
// So it does in a nested function declared after it (§8.1, §11.1).
let K = 3;

fn main() {
    fn K(x: i64) -> i64 { x }
    fn g(x: i64) -> i64 { match x { K => 1, _ => 2 } }
    print(g(3), " ", K(1));
}
