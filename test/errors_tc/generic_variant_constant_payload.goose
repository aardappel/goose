// error: variant Opt<u8>.Some has a payload; construct it with Opt<u8>.Some { ... }
// A generic enum's variant named without a body is a payload-less
// constant (§3.5); one with a payload needs its literal, defaults or not.
enum Opt<T> { None, Some { v: T = 0 } }
fn main() { let o = Opt<u8>.Some; print(o); }
