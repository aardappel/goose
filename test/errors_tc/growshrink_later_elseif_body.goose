// error: cannot pop a while w (bound at
// error: is still used: it may refer into a
// The same for an else-if's condition and body (§5.2).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    let w = a[1..];
    let c = a.len > 5;
    if c { print(0); } else if a.pop() > 0 { print(w); }
    print(a.len);
}
