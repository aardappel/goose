// error: storing a reference into a, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A literal never holds a reference into a grow-shrink array (§5.2), so the
// pop later in the statement never meets one held by a temporary.
struct Holder { r: i64& }
fn read(value: Holder, unused: i64) { print(value.r); }
fn main() {
    var a: i64[>..<] = [42];
    read(Holder { a[0] }, a.pop());
}
