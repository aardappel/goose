// error: enum Opt has no variant named Nothing
enum Opt<T> { None, Some { v: T } }
fn main() { let o = Opt<u8>.Nothing; print(o); }
