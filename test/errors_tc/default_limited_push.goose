// error: cannot .push through a non-writable value
// default<T>() of a limited array is an empty array built in a temporary,
// read-only as a literal is (§9.2, §9.5): pushing into it would hand out a
// reference into storage that is gone once the statement ends.
var keep: i64&? = null;
fn grab() { keep .= default<i64[..4]>().push(5); }
fn main() {
    grab();
    if keep { print(keep); }
}
