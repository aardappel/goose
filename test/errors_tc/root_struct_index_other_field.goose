// error: .index_of needs a reference rooted at the array itself
root struct S { a: i64[>..], b: i64[>..] }
fn main() {
    var s = S { [1], [2] };
    let r .= s.a[0];
    print(s.b.index_of(r));
}
