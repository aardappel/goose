// error: binding s to a slice rooted at a temporary, which does not outlive it
// Each name a multi-value call binds is held to the same rule.
fn mk() -> u8[] { "hello" }
fn two(xs: const u8[:]) -> const u8[:], i64 { return xs, xs.len; }
fn main() {
    let s, n = two(mk());
    print(s, n);
}
