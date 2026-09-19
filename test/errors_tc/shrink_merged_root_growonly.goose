// error: cannot pop b (which r may point at) while s is still used: it may hold a reference or slice into it
// A grow-only shrink through a reference whose root is a merge covers every
// array the root bounds, not only the one it names (§5.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len < b.len;
    let r: i64[>..]& = if c { a } else { b };
    let s = b[..];
    r.pop();
    b.push(77);
    print(s[1], " ", s.len);
}
