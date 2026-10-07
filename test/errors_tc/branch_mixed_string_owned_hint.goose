// error: for byte-string branches (including mixed literals and builders), declare an explicitly owned result: `let text: u8[>..] = if c { a } else { b };`
import std;
fn main() {
    let n = 3;
    let text = if n == 0 { "none" } else { str("count ", n) };
    print(text);
}
