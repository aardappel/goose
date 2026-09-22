// error: read-only
struct S { x: i64 }
struct Both { a: S, b: S }
fn format(out: u8[>..]&, v: S&) { v.x = 99; format(out, v.x); }
fn main() { const b = Both { S { 1 }, S { 2 } }; print(b); }
