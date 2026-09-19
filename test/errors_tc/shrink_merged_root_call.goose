// error: cannot call drop, which may shrink b while s
// error: is still used: it may refer into b
// drop shrinks what its argument points at, and pick's result may be a or b:
// the call counts as shrinking both (§5.2).
fn drop(xs: i64[>..<]&) -> i64 { xs.pop() }
fn pick(c: bool, a: i64[>..<]&, b: i64[>..<]&) -> i64[>..<]& { if c { a } else { b } }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len < b.len;
    let s = b[..];
    drop(pick(c, a, b));
    b.push(77);
    print(s[1], " ", s.len);
}
