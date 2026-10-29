// error: queue elements must be flat
root struct S { n: i64 }
fn main() {
    var s = S { 1 };
    qput(s);
}
