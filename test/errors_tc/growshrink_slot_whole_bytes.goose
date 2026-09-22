// error: cannot clear p.y while v
// error: is still used: it may refer into p.y
// The bytes of what the reference in p.y leads to are a grow-shrink array's
// elements (§5.2).
struct P { y: i64[>..<]& }
fn main() {
    var b: i64[>..<] = [4, 5];
    var p = P { y: b };
    let v = p.y.bytes_of();
    p.y.clear();
    print(v);
}
