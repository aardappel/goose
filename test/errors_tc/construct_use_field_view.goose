// error: cannot use t.view in the value assigned to t.chars
// Another field of the value is not rebuilt, but a slice in it may view the
// array that is.
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
