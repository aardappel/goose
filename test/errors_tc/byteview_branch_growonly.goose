fn main() {
    var a: i64[>..] = [42];
    let b = if a.len > 0 { a.bytes_of() } else { a.bytes_of() };
    a.clear();
    print(b);
}
