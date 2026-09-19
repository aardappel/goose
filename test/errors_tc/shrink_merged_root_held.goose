// error: cannot pop r: an earlier expression value
// error: may still refer into b, which r may point at
// The slice of b is held by the call while its next argument pops through r,
// which may point at b (§5.2).
fn total(s: const i64[:], n: i64) -> i64 {
    var t = n;
    for x in s { t += x; }
    t
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let c = a.len < b.len;
    let r: i64[>..<]& = if c { a } else { b };
    print(total(b[..], r.pop()));
}
