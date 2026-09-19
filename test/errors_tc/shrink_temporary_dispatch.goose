// error: storing a reference into r, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// wrap's parameter stands for a[0], and a literal never holds a reference
// into a grow-shrink array (§5.2), so the pop later in the statement never
// meets one held by the dispatch's temporary.
enum E { A { r: i64& }, B { r: i64& } }
fn read(v: E.A, n: i64) { print(v.r); }
fn read(v: E.B, n: i64) { print(v.r); }
fn wrap(r: i64&) -> E { E.A { r } }
fn main() {
    var a: i64[>..<] = [42];
    read(wrap(a[0]), a.pop());
}
