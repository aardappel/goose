// error: returning a reference rooted in local, which dies with this function
fn bad() -> i64[:] {
    var local: i64[1] = [42];
    return block { break local[..]; };
}
fn main() { let view = bad(); print(view[0]); }
