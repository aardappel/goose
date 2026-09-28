// error: parameter b has a default but no type
// A default is a value of its parameter's type, which an untyped parameter
// has only from an argument (§7.1).
fn f(a, b = 1) { print(a, b); }
fn main() { f(1); }
