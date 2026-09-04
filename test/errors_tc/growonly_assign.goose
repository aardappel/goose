// Assigning a grow-only array whole is a shrink (§5.1).
fn main() {
    var g: i64[>..] = [1, 2, 3];
    let s = g[0..2];
    g = [9];
    print(s[1]);
}
