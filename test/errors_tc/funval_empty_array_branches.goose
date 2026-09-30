// error: cannot infer the element type of []: a block's value is checked without its call's destination
// Neither branch gives the block's value an element type, and the call that
// discards it gives none either (§7.6).
fn run<F>(c: bool) {
    F(c);
}
fn main() {
    run(true) { if it { [] } else { [] } }
    print(1);
}
