// error: guard is a statement: it guards the rest of the block it is in
fn main() {
    let x = 1;
    match x { 1 => guard x > 0 else { return; }, _ => {} }
}
