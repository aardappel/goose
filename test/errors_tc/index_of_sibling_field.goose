// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at s, which may hold i64 values outside that array
// A root names a variable, not a part of it: an element of a sibling field is
// rooted at s exactly as the receiver is, but is not one of its elements, and
// its index would lie past the end of `a` (§3.3).
struct S { a: i64[4], b: i64[4] }
fn main() {
    var s = S { a: [1, 2, 3, 4], b: [5, 6, 7, 8] };
    let i = s.a.index_of(s.b[3]);
    s.a[i] = 9;
    print(s.b[3]);
}
