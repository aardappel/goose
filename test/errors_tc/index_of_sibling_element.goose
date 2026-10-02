// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at p, which may hold i64 values outside that array
// Each element of p is an array of i64s, so a reference into another one is
// rooted at p as the receiver is, without being one of its elements (§3.3).
fn main() {
    var p: i64[4][2] = [[1, 2, 3, 4], [5, 6, 7, 8]];
    print(p[0].index_of(p[1][2]));
}
