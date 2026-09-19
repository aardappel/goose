// error: cannot call f, which may shrink b while s
// error: is still used: it may refer into b
// r's root is z's, which bounds pp.y's array as well: popping through r may
// shrink the caller's b, which pp leads to (§5.2, §9.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn f(pp: P&, c: bool) {
    var z: i64[>..<] = [0];
    let r: i64[>..<]& = if c { z } else { pp.y };
    r.pop();
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    var p = P { x: a, y: b };
    let s = b[..];
    f(p, a.len > 5);
    b.push(77);
    print(s[1]);
}
