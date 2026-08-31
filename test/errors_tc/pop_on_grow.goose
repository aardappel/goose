// Shrink operations exist only on grow-shrink and limited arrays (§3.3).
fn main() {
    var g: i64[>..] = [1];
    print(g.pop());
}
