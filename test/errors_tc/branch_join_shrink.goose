// error: cannot pop g while s is still used
// The joined slice views g (§6.4), so g may not shrink while it is used
// (§5.1).
fn main() {
    var g: u8[>..] = [];
    g.push(65);
    g.push(66);
    let c = g.len > 5;
    let s = if c { "x" } else { g };
    g.pop();
    print(s);
}
