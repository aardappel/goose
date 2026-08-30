// Definite assignment: no reads of uninitialized locals (§4.4).
fn main() {
    var x: int;
    if x > 0 { x = 1; }
    print(x);
}
