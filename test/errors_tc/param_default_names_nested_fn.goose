// error: the default of parameter b of f names bonus, a nested function
// Around f's declaration `bonus` is the nested function, so the default does
// not quietly call the top-level one of that name either (§7.1).
fn bonus() -> i64 { 1 }
fn main() {
    fn bonus() -> i64 { 2 }
    fn f(a: i64, b: i64 = bonus()) -> i64 { a + b }
    print(f(1));
}
