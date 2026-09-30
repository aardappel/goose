// error: array fill count K names a local variable, not a constant
// A fill count names constants as a size does (§3.3, §4.2): the local K
// hides the global one.
let K = 3;

fn main() {
    let K = 5;
    let a = [7; K];
    print(a.len, " ", K);
}
