// Ambiguous resolution is an error (§7.1).
fn f(a: i64, b) { print(a); print(b); }
fn f(a, b: i64) { print(a); print(b); }
fn main() {
    f(1, 2);
}
