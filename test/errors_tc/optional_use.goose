// Optionals must be narrowed before access (§3.8).
fn f(o: i64?) -> i64 {
    o + 1
}
fn main() {
    var x = 1;
    print(f(&x));
}
