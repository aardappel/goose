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
var state = 0;
fn effect() -> i64 { state++; state }
fn main() {
    let options = Options { c: effect(), b: 2, a: 1 };
    print(options.a);
}
