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
enum Sexp_se {
    Sym  { name: u8[varint] },
    List { kids: (Sexp_se..&<varint>)[varint] },
}
var pool: Sexp_se..[>..] = [];

// A.3 A growable hash table (open addressing), in-model.
struct Slot_se { key: i64 = 0, val: i64 = 0 }
struct Map { slots: Slot_se[>..] }

// A.4 Case functions as virtuals.
enum Node_se { Num { v: f64 }, Add { l: Node_se..&<u16>, r: Node_se..&<u16> } }
recursive fn eval_se(n: Node_se.Num&) -> f64 { n.v }
recursive fn eval_se(n: Node_se.Add&) -> f64 { eval_se(n.l) + eval_se(n.r) }

// A.5 Long-distance errors: parse returns a value and an error string; deep
// failures land at parse via `return ... from`.
struct Lexer { pos: i64, limit: i64 }
enum Expr { Nothing, Something { v: i64 } }

fn tokens_remain(l: Lexer&) -> bool { l.pos < l.limit }

fn parse_expr(l: Lexer&) -> Expr.. {
    guard l.tokens_remain() else { return Expr.Nothing {}, "eof" from parse; }
    Expr.Something { v: 1 }
}

fn parse(src: u8[:]) -> Expr.., u8[] {
    var l = Lexer { pos: 0, limit: src.len };
    return parse_expr(l), "";
}

fn main_spec_examples() {
    let words = read_words("hello world");
    print(words.len);
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_spec_examples(); }
