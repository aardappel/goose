// error: expected a value of type i64&, got i64?
// A var's narrowing does not hold in a defer body: a rebinding between the
// defer and an exit may have undone it.
fn main() {
    var x = 5;
    var p: i64&? = x;
    guard p else { return; }
    defer { let q: i64& = p; print(q); }
    p .= null;
}
