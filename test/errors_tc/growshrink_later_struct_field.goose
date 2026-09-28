// error: cannot pop a while w (bound at
// error: is still used: it may refer into a
// A literal's later fields are built after the one that pops the grow-shrink
// array the slice views (§5.2).
struct U { n: i64, m: i64 }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    let w = a[1..];
    let u = U { n: a.pop(), m: w[1] };
    print(u.m);
}
