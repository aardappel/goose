// Narrowing is killed by rebinding the variable (§3.8).
fn f(var o: i64?, p: i64?) -> i64 {
    guard o else { return 0; }
    o .= p;
    o
}
fn main() {
    var x = 1;
    print(f(&x, &x));
}
