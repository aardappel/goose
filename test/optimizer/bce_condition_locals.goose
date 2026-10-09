// A predicate inlined into a loop condition binds its parameter as a local
// of the condition. Declaring that local changes nothing the condition's
// comparisons read, so `p < len` still holds for the predicate's index and
// for the body. A call in the condition that moves an outer variable does
// cut the facts (bce_effectful_shortcircuit.goose).
import std;

fn is_blank(c: u8) -> bool { return c == ' ' || c == '\n'; }

fn skip_blank(text: u8[:], p0: i64) -> i64 {
    guard p0 >= 0 else { return 0; }
    var p = p0;
    while p < text.len && is_blank(text[p]) { p++; } // bce:elide
    return p;
}

fn main() {
    var ks: i64[>..] = [1];
    var text: u8[>..] = [];
    text.append("a  \n b");
    print(skip_blank(text, ks[0])); // 5
}
