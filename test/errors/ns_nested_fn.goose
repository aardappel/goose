// Nested functions are lexical; only top-level declarations take a namespace.
fn outer() {
    fn a::inner() {}
}
fn main() {}
