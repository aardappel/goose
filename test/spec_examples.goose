// The worked examples of the spec (Appendix A), padded with enough
// declarations to parse standalone. Parse-only: not meaningful programs.

// A.1 A list of strings, flat.
fn read_words(text: u8[:]) -> u8[][>..] {
    var words: u8[][>..] = [];
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' { i++; }
        words.push(text[start..i]);
        while i < text.len && text[i] == ' ' { i++; }
    }
    return words;
}

// A.2 A compact tree with relative references.
enum Sexp {
    Sym  { name: u8[varint] },
    List { kids: (Sexp..&<varint>)[varint] },
}
var pool: Sexp..[>..] = [];

// A.3 A growable hash table (open addressing), in-model.
struct Slot { key: int = 0, val: int = 0 }
struct Map { slots: Slot[>..] }

// A.4 Case functions as virtuals.
enum Node { Num { v: flt }, Add { l: Node..&<u16>, r: Node..&<u16> } }
recursive fn eval(n: Node.Num&) -> flt { n.v }
recursive fn eval(n: Node.Add&) -> flt { eval(n.l) + eval(n.r) }

// A.5 Long-distance errors.
struct Lexer { pos: int, limit: int }
struct ParseError { msg: u8[] }
enum Expr { Nothing, Something { v: int } }

fn tokens_remain(l: Lexer&) -> bool { l.pos < l.limit }

fn parse_expr(l: Lexer&) -> Expr.. {
    guard l.tokens_remain() else { return ParseError { msg: "eof" } from parse; }
    Expr.Something { v: 1 }
}

fn parse(src: u8[:]) -> Expr.. {
    var l = Lexer { pos: 0, limit: src.len };
    return parse_expr(&l);
}

fn main() {
    let words = read_words("hello world");
    print(words.len);
}
