// error: expression expected, found '}'
// error: expression expected, found ','
// error: expression expected, found '}'
// A statement's error inside braces the statement opened, a struct
// literal's or a match's, loses the statement through the `}` closing
// them, by the lexer's count of the braces open, and the block goes on
// after it (docs/design/language_server.md §5 D): neither that `}` nor a
// keyword before it ends the block. The language server publishes each
// error where it is (test/lsp_protocol.py opens this file).
struct P { x: i64, y: i64 }
fn literal(a: i64) -> i64 {
    let p = P { x: 1, y: };
    let q = a;
    q
}
fn arms(v: i64) -> i64 {
    let w = match v { 1 => 2 +, _ => if v > 0 { 3 } else { 4 } };
    let q = v;
    q
}
fn lines(a: i64) -> i64 {
    let p = P {
        x: a,
        y:
    }
    print(a);
    a
}
fn main() {
    print(literal(1) + arms(1) + lines(1));
}

// lsp: error 12
// lsp: error 17
// lsp: error 25
