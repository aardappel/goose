// error: cannot pop g: the for loop at
// error: iterates g[..], which may refer into g
// The slice a for loop walks is live for its whole body, so a grow-only array
// may not shrink under it either: the next iteration would read the slot the
// push refilled (§5.1, §6.5).
fn main() {
    var g: f64[>..] = [1.0, 2.0, 3.0, 4.0];
    for y in g[..] {
        g.pop();
        g.push(0.5);
        print(y);
    }
}
