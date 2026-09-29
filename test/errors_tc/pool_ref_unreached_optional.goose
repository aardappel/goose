// error: .alloc_index exists on reusable pools only
// Checked standalone, an optional parameter is no pool reference, since no
// call could give it one (§5.4).
fn never(p: i64[>..]&?) -> i64 {
    if p { return p.alloc_index(1); }
    return -1;
}
fn main() { print(0); }
