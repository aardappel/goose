// error: cannot assign h while s
// error: is still used: it may refer into h
// The right-hand side of a whole assignment runs after its shrink (§5.2).
fn collect(s: i64[:]) -> i64[>..<] {
    var out: i64[>..<] = [];
    for c in s { out.push(c + 1); }
    for c in s { out.push(c + 2); }
    return out;
}
fn main() {
    var h: i64[>..<] = [1, 2, 3];
    let s = h[..];
    h = collect(s);
    print(h);
}
