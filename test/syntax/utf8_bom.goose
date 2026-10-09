// A source file may start with a UTF-8 byte-order mark, which the lexer
// skips (spec §2): this one does.
fn main() {
    let s = "café";
    print(s.len, " ", s);
}
