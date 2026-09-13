// error: cannot clear line while parts is in scope: a reference into it is stored there
// error: , which the next iteration reaches
// The store comes after the clear in the loop body, so the next iteration's
// clear would invalidate it (§5.1).
import std;
fn main() {
    var line: u8[>..] = [];
    var parts: u8[:][>..] = [];
    for i in 3 {
        line.clear();
        format(line, "word ", i);
        parts.push(line[..]);
    }
    print(parts.len);
}
