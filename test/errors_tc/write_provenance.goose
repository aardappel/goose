// Writing through a slice derived from a string literal: non-writable
// provenance, reported at the offending instantiation (§9.5).
fn zap(s: u8[:]) {
    s[0] = 65;
}

fn main() {
    zap("static data");
}
