// error: re-binding best with a reference rooted at a different scope depth is not supported
// error: best is bound to one rooted at text, and this one is rooted at the caller's storage behind d (freq)
// A key the dictionary's each reads out of freq is rooted at freq, which
// each was given, whatever freq's keys view (§9.5): freq is declared inside
// the block, deeper than text, so best cannot move from a view of text to
// it (§9.2).
import std;
import dictionary;

fn twice<F>() { F(0); F(1); }

fn main() {
    var text: u8[>..] = [];
    text.append("a bb a");
    twice() { r =>
        var freq = dictionary<u8[:], i32> {};
        each_word(text) { w => freq.update(w, 0) { it += 1; }; };
        var best = text[0..0];
        freq.each() { w, c => if c > 1 { best = w; } };
        print(r, " ", best);
    };
}
