// error: defer takes a statement or a block, not `let`
fn main() {
    defer let x = 1;
}
