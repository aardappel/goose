// error: cannot pop a while w (bound at
// error: is still used: it may refer into a
// The body of an if runs after its condition, which pops the grow-shrink
// array the slice views (§5.2).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    let w = a[1..];
    if a.pop() > 0 { print(w); }
}
