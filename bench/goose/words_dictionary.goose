// words.goose counted in the standard library's dictionary instead of the
// hand-rolled table: slice keys into the text, growth by rehash, the
// update-in-place block. The row measures the general-purpose container
// against the purpose-built one.

import rng;
import dictionary;

let N = 2000000;                         // BENCH_N
let MAXW = 20;

fn main() {
    let vocab = N / 10 + 16;
    var r: u64 = 12345;
    // A vocabulary of fixed-capacity words, so it stays randomly indexable.
    var dict: u8[..MAXW][>..] = [];
    for i in vocab {
        var w: u8[..MAXW] = [];
        r = xs_next(r);
        let n = 3 + xs_mod(r, 12);
        for j in n {
            r = xs_next(r);
            w.push(('a' + xs_mod(r, 26)) as! u8);
        }
        dict.push(w);
    }
    // A text of N words drawn with a skew towards the front of the vocabulary.
    var text: u8[>..] = [];
    for i in N {
        r = xs_next(r);
        let a = xs_mod(r, vocab);
        let k = (a * a) / vocab;
        text.append(dict[k]);
        text.push(' ');
    }
    // Split: one contiguous block of variable-size strings.
    var words: u8[][>..] = [];
    each_split(text, ' ') { if it.len > 0 { words.push(it); } };
    // Count.
    var counts = dictionary<u8[:], i32> {};
    for w in words { counts.update(w[..], 0) { it += 1; } }
    var total = 0;
    counts.each() { k, c => total += c * (k.len + k[0]); };
    print(words.len);
    print(counts.count);
    print(total);
}
