// error: cannot assign x while s is still used: it may hold a reference or slice into it
// The right-hand side of a whole assignment runs after its shrink, so a
// slice it uses is live there (§5.1).
fn collect(s: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for c in s { out.push(c + 1); }
    for c in s { out.push(c + 2); }
    return out;
}
fn main() {
    var x: u8[>..] = [];
    x.append("abcd");
    let s = x[..];
    x = collect(s);
    print(x);
}
