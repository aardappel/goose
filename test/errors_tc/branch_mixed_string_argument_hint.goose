// error: for byte-string branches (including mixed literals and builders), declare an explicitly owned result
import std;
fn take(text: const u8[:]) { print(text); }
fn main() {
    let n = 3;
    take(if n > 0 { str("count ", n) } else { "none" });
}
