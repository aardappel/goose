// error: cannot assign t while t is still used: a reference into it was stored there
// Another field of the value is not rebuilt, but a slice in it views the
// array that is, and the right-hand side reads it after the shrink the
// assignment starts with (§4.4, §5.1).
struct Text { view: u8[:], chars: u8[>..] }
fn collect(s: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    out.append(s);
    return out;
}
fn main() {
    var spare: u8[>..] = [];
    var t = Text { view: spare[..], chars: [] };
    t.chars.append("abcd");
    t.view = t.chars[1..];
    t.chars = collect(t.view);
    print(t.chars);
}
