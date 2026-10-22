// error: unknown escape sequence: \q
// error: illegal character: '@'
// error: illegal character: '©' (U+00A9)
// error: expected closing ' of character literal
// error: unterminated string literal
// error: unterminated """ string
// error: unterminated /* comment
// A token the lexer rejects is reported, and the parse goes on after its
// line, or after the closing quotes of a """ string
// (docs/design/language_server.md §5 D). A comment the file ends in ends
// the blocks still open there as well, which report nothing more.
fn main() {
    let s = "bad\q";
    let e = 3 @ 4;
    let u = 3 © 4;
    let c = 'ab';
    let t = "open
    print(s, e, c, t);
    let m = """
      unterminated
    if m {
        print(m);
/* unterminated
