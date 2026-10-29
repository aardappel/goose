// error: nested root owners are not supported
root struct Inner { items: i64[>..] }
root struct Outer { inner: Inner }
fn main() {}
