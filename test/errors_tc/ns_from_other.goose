// A `from` target is the declaration the name resolves to, not any function
// with that leaf name: b::outer never catches a return aimed at a::outer.
fn a::deep() -> i64 { return -1 from outer; }
fn a::outer() -> i64 { a::deep() }
fn b::outer() -> i64 { a::deep() }
fn main() { print(b::outer()); }
