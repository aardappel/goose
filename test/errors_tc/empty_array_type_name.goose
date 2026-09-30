// error: expected a value of type i64, got []
// A [] that nothing gave an element type has no type a program can name:
// diagnostics show it as the literal.
fn main() {
    let x: i64 = [];
    print(x);
}
