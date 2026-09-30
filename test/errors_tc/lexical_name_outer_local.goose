// error: type parameter N is bound to the type i64, not a value
// Inside g its type parameter N hides main's local N (§11.1).
fn main() {
    let N = 3;
    fn g<N>(x: N) -> i64 { N }
    print(g(1), " ", N);
}
