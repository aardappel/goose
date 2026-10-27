// error: defer body calls each, which names out, which build returns by name and so builds at its destination (§7.3): a defer runs after the value is complete (§6.6)
// A named function value passed on in the body names the result through the
// higher-order function's record.
fn each<F>(xs: i64[:]) { for x in xs { F(x); } }
fn build() -> i64[>..] {
    var out: i64[>..] = [];
    fn grow(x: i64) { out.push(x); }
    grow(5);
    defer each([1, 2], grow);
    return out;
}
fn main() { print(build()); }
