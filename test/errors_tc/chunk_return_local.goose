// error: returning a reference rooted in local, which dies with this function
// A chunk is rooted where its array is, as a slice of it is (§9.2).
fn first_half() -> i64[:] {
    var local: i64[4] = [1, 2, 3, 4];
    return local.chunk(0, 2);
}
fn main() { print(first_half()); }
