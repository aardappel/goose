// error: cannot pop p.x while s
// error: is still used: it may refer into p.x
// p holds references to a and b, so p.x, read back out of it, may point
// into either (§9.5): popping through it counts as popping either (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let p = P { x: a, y: b };
    let s = a[..];
    p.x.pop();
    a.push(77);
    print(s[2]);
}
