// error: defer body names out, which build returns by name and so builds at its destination (§7.3): a defer runs after the value is complete (§6.6)
// Reads are rejected with writes: the rule is about naming the local.
fn build(n: i64) -> u8[>..] {
    var out: u8[>..] = "x";
    defer print(out.len);
    for i in n { out.push(97); }
    return out;
}
fn main() { print(build(3)); }
