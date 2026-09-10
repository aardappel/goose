// A `const` parameter is read-only whatever it is given (§9.5).
fn zap(s: const u8[:]) { s[0] = 65; }
fn main() {
    var text: u8[>..] = "abc";
    zap(text);
}
