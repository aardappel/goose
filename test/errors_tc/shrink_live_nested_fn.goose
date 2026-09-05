// A nested function called after the shrink reads the slice (§5.1).
import std;
fn main() {
    var line: u8[>..] = [];
    line.append("hello");
    let w = line[0..2];
    fn show() { print(w); }
    line.clear();
    show();
}
