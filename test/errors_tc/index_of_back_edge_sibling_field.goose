// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at the caller's storage behind a, which may hold i64 values outside that array
// The entry call passes a whole array, the recursive call two fields of one
// struct: the back edge cannot reuse a body that took r to be an element of
// a, so it is checked as a call of its own (§3.3, §7.8).
struct S { a: i64[4], b: i64[4] }
var s = S { a: [1, 2, 3, 4], b: [5, 6, 7, 8] };
recursive fn at(a: i64[4]&, r: i64&, n: i64) -> i64 {
    if n == 0 { return at(s.a, s.b[3], 1); }
    return a.index_of(r);
}
fn main() {
    var t: i64[4] = [1, 2, 3, 4];
    print(at(t, t[2], 0));
}
