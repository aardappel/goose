// error: cannot call drop2, which shrinks b while s
// error: is still used: it may refer into b
// Inside drop2 the merged reference may point at either parameter's array, so
// its summary shrinks both, and the call is checked against both (§5.2).
fn drop2(c: bool, a: i64[>..<]&, b: i64[>..<]&) -> i64 {
    let r: i64[>..<]& = if c { a } else { b };
    r.pop()
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len < b.len;
    let s = b[..];
    drop2(c, a, b);
    b.push(77);
    print(s[1], " ", s.len);
}
