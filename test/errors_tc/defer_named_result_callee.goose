// error: defer body calls grow, which names out, which build returns by name and so builds at its destination (§7.3): a defer runs after the value is complete (§6.6)
// A nested function naming the result, checked before the defer and reused
// in it, is caught through what its check recorded of the variables outside
// it.
fn build() -> i64[>..] {
    var out: i64[>..] = [];
    fn grow() { out.push(1); }
    grow();
    defer grow();
    return out;
}
fn main() { print(build()); }
