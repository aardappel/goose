// error: defer body names out, which build returns by name and so builds at its destination (§7.3): a defer runs after the value is complete (§6.6)
// A function value written in the body that names the local counts too.
fn each<F>(xs: i64[:]) { for x in xs { F(x); } }
fn build(n: i64) -> i64[>..] {
    var out: i64[>..] = [];
    defer each([1, 2]) { out.push(it); };
    for i in n { out.push(i); }
    return out;
}
fn main() { print(build(3)); }
