// Ambiguous resolution is an error (§7.1).
fn f(a: int, b) { print(a); print(b); }
fn f(a, b: int) { print(a); print(b); }
fn main() {
    f(1, 2);
}
