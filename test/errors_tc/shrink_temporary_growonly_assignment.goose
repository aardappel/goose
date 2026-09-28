// error: cannot call remove, which shrinks a: r, which the assignment writes after it
// error: may be in a
fn remove(a: i64[>..]&) -> i64 { a.pop() }
fn main() {
    var a: i64[>..] = [42];
    let r .= a[0];
    r = remove(a);
}
