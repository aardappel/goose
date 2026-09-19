// error: cannot pop r while s
// error: is still used: it may refer into b, which r may point at
// r's root is a merge of a's and b's, which only bounds what it points at, so
// popping through it counts as popping either array (§5.2, §9.2).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len < b.len;
    let r: i64[>..<]& = if c { a } else { b };
    let s = b[..];
    r.pop();
    b.push(77);
    print(s[1], " ", s.len);
}
