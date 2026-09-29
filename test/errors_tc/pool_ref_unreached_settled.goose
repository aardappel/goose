// error: .alloc_index exists on reusable pools only
// Checked standalone, a parameter nothing passes a pool to may be a pool
// reference or not; rebound to an array that is not a pool, it is not one,
// whatever a call would pass (§5.4).
var plain: i64[>..] = [1];
fn never(var p: i64[>..]&) {
    p .= plain;
    print(p.alloc_index(2));
}
fn main() { print(plain.len); }
