// runtime-debug: validate before allocation or metadata changes.
fn main() {
    var a: i64[>..] = [];
    print(a.check_length(-1));
}
