// The ownership diagnostic's fix works for either branch order and match.
import std;
fn show(n: i64) {
    let text: u8[>..] = if n == 0 { "none" } else { str("count ", n) };
    let reversed: u8[>..] = if n > 0 { str("count ", n) } else { "none" };
    let matched: u8[>..] = match n { 0 => "none", _ => str("count ", n) };
    assert(text == reversed && text == matched);
    print(text);
}
fn main() { show(0); show(3); }
