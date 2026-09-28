// error: cannot clear c: the for loop at
// error: iterates c[1..], which may refer into c
// An enclosing loop's sequence is live throughout the loops inside it
// (§5.2, §6.5).
fn main() {
    var c: i64[>..<] = [1, 2, 3];
    var d: i64[>..<] = [4, 5];
    for x in c[1..] {
        for y in d[..] {
            if x + y > 6 { c.clear(); }
        }
        print(x);
    }
}
