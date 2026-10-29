// error: still under construction
root struct S { first: u8[][>..], second: u8[][>..] }
fn first(s: S&) -> u8[] { s.first.push("nested"); return "outer"; }
fn build(s: S&) { s.first.push(first(s)); }
fn main() { var s = S { .. }; build(s); }
