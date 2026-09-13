// error: copy of a temporary: the value is fresh already
// copy() copies stored values; a temporary is fresh already (§4.1).
fn make() -> i64[>..] { [1, 2] }
fn main() {
    var xs: i64[>..] = copy(make());
    print(xs.len);
}
