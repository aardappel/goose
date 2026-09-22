// error: cannot clear p.y while x
// error: is still used: it may refer into p.y
// Iterating through the reference in p.y walks the elements of the whole
// grow-shrink array it leads to (§5.2).
struct P { y: i64[>..<]& }
fn main() {
    var b: i64[>..<] = [4, 5];
    var p = P { y: b };
    for &x in p.y {
        p.y.clear();
        print(x);
    }
}
