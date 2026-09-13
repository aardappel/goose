// error: cannot clear line while parts is still used: a reference into it was stored there
// A slice of the buffer was stored into `parts`, which is still in scope
// at the clear (§5.1).
import std;
fn main() {
    var line: u8[>..] = [];
    line.append("a b c");
    var parts: u8[:][>..] = [];
    each_split(line, ' ') { parts.push(it); };
    line.clear();
    print(parts.len);
}
