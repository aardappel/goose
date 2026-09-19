// error: unknown identifier: j
// A nested function sees the variables in scope where it is declared, not
// one its block declares further on.
fn main() {
    fn h() -> i64 { j }
    let j = 5;
    print(h());
}
