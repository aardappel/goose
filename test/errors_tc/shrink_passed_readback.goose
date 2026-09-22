// error: cannot call consume: it shrinks a while r is still used, and r may refer into a
// A reference read back out of a holder is rooted by a bound on what may
// own its target, here the caller's own variables, which its callers cannot
// name: the call making it an argument beside the array is judged there
// (§5.1, §9.5).
struct H { r: i64& }
fn consume(r: i64&, a: i64[>..]&) {
    a.clear();
    a.push(99);
    print(r);
}
fn mid(a: i64[>..]&) {
    var x = 1;
    let h = H { a[0] };
    consume(h.r, a);
    print(x);
}
fn main() {
    var values: i64[>..] = [8, 9];
    mid(values);
}
