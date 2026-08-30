// Shrink operations exist only on grow-shrink and limited arrays (§3.3).
fn main() {
    var g: int[>..] = [1];
    print(g.pop());
}
