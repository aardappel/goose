// error: still used
root struct S { value: i64&, other: i64[>..] }
fn main() {
    var a: i64[>..] = [1];
    var s = S { a[0], [] };
    a.clear();
    print(s.value);
}
