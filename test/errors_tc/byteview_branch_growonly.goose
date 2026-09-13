// error: cannot clear a while b is still used: it may hold a reference or slice into it
fn main() {
    var a: i64[>..] = [42];
    let b = if a.len > 0 { a.bytes_of() } else { a.bytes_of() };
    a.clear();
    print(b);
}
