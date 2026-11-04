// lsp: fixture for test/lsp_protocol.py: names after characters that take
// two to four bytes in UTF-8 and one or two units in UTF-16 (é, ∑, 🦆), on
// the lines of a declaration, a use and an error, asked about in both
// position encodings. The checks find their columns in the text.
fn main() {
    let s = "é∑🦆"; let total = s.len;
    print("🦆🦆", total);
}

fn broken() -> i64 { let t = "é🦆"; missing }
