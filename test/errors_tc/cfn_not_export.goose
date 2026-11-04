// error: cfn's argument names an export fn, whose C wrapper C calls back
// C calls back the C wrapper an export fn has; a plain function has none
// (spec §7.10).
fn cb(v: i32) -> i32 { v }
fn main() { let p = cfn(cb); print(p); }
