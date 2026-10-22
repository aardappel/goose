// error: build calls into its recursive cycle through build, whose non-fixed-size result would land in a temporary here
// A parameter's default is evaluated at the call leaving it out (§7.1), as
// that call's argument: a call into the cycle there whose result is the
// argument lands in a temporary, held across the call (§7.8).
var depth: i64 = 3;
fn dec() -> i64 { depth -= 1; return depth; }
fn label(s: u8[] = build(dec())) -> i64 { s.len }
recursive fn build(n: i64) -> u8[] {
    if n <= 0 { return "x"; }
    let k = label();
    return str(k);
}
fn main() { print(build(3)); }
