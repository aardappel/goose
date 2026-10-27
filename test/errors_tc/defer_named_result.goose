// error: defer body names out, which build returns by name and so builds at its destination (§7.3): a defer runs after the value is complete (§6.6)
// A returned local is built where the caller receives it, and its count is
// recorded by the return; a defer running after cannot be allowed to touch
// it.
fn build(n: i64) -> i64[>..] {
    var out: i64[>..] = [];
    defer out.push(n);
    for i in n { out.push(i); }
    return out;
}
fn main() { print(build(3)); }
