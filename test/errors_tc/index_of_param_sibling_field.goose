// error: .index_of needs a reference rooted at the array itself
// error: this one is rooted at the caller's storage behind a, which may hold i64 values outside that array
// Two arguments rooted at one variable share a parameter class, which says
// only that they lie in the same storage: where that storage holds i64s
// outside one array, the class does not make r an element of a (§3.3).
struct S { a: i64[4], b: i64[4] }
fn at(a: i64[4]&, r: i64&) -> i64 { a.index_of(r) }
fn main() {
    var t: i64[4] = [1, 2, 3, 4];
    print(at(t, t[2]));
    var s = S { a: [1, 2, 3, 4], b: [5, 6, 7, 8] };
    print(at(s.a, s.b[3]));
}
