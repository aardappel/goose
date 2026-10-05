// error: field initializers must follow declaration order: b comes before c
// error: supplied fields in declaration order: a, b, c
// The complete repair includes a field after the first out-of-order name,
// and excludes omitted defaults, optional fields and padding.
struct Options {
    unused: bool = false,
    a: i64,
    pad,
    optional: i64&?,
    b: i64,
    another_default: i64 = 10,
    c: i64,
}
fn main() {
    let options = Options { c: 3, b: 2, a: 1 };
    print(options.a);
}
