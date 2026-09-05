// A slice declared outside a loop and read inside it is read again after a
// shrink in the same loop, on the next iteration (§5.1).
import std;
fn main() {
    var line: u8[>..] = [];
    line.append("hello");
    let w = line[0..2];
    for i in 3 {
        print(w);
        line.clear();
        line.append("x");
    }
}
