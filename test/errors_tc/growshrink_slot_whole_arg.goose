// error: cannot pop p.y: an earlier expression value
// error: may still refer into p.y
// The reference in p.y, read out of a field, leads to a whole grow-shrink
// array, which a field may hold a reference to (§5.2): the slice of it
// passed as the first argument views its elements, which the pop frees.
struct P { y: i64[>..<]& }
fn total(s: const i64[:], n: i64) -> i64 {
    var t = n;
    for x in s { t += x; }
    t
}
fn main() {
    var b: i64[>..<] = [4, 5];
    var p = P { y: b };
    print(total(p.y, p.y.pop()));
}
