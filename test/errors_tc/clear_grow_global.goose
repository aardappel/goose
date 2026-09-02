// clear on a grow-only array applies to locals only (§5.1): a global's
// references can be held anywhere in the program.
var g: u8[>..] = [];
fn main() {
    g.push(1);
    g.clear();
    print(g.len);
}
