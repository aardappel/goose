// error: a nested function cannot be declared into a namespace
// Nested functions are lexical; only top-level declarations take a namespace.
fn outer() {
    fn a::inner() {}
}
fn main() {}
