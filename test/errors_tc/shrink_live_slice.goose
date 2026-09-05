// A slice used after the shrink keeps the array from shrinking (§5.1).
import std;
fn main() {
    var line: u8[>..] = [];
    line.append("hello");
    let w = line[0..2];
    line.clear();
    print(w);
}
