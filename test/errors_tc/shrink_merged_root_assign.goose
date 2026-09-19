// error: cannot assign r while s
// error: is still used: it may refer into b, which r may point at
// Assigning a grow-shrink array whole through a merged reference shrinks
// whichever array it points at (§5.2).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len < b.len;
    let r: i64[>..<]& = if c { a } else { b };
    let s = b[..];
    r = [9];
    b.push(77);
    print(s[1]);
}
