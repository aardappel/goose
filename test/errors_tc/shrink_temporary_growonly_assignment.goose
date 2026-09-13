// error: cannot call remove, which shrinks: an earlier expression value
// error: may still refer into a
fn remove(a: i64[>..]&) -> i64 { a.pop() }
fn main() {
    var a: i64[>..] = [42];
    let r .= a[0];
    r = remove(a);
}
