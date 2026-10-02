// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at r
// The entry call passes an array and one of its elements, which share a
// class; the recursive call passes two different arrays, so it cannot reuse
// a body that took r to be an element of a (§3.3, §7.8).
var g1: i64[>..] = [1];
var g2: i64[>..] = [2, 3, 4];
recursive fn at(a: i64[>..]&, r: i64&, n: i64) -> i64 {
    if n == 0 { return at(g1, g2[2], 1); }
    return a.index_of(r);
}
fn main() {
    var t: i64[>..] = [1, 2, 3, 4];
    print(at(t, t[2], 0));
}
