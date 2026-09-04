// Assigning the array whole is a shrink (§5.2).
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let s = gs[..];
    gs = [9];
    print(s[0]);
}
