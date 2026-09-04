// strlist.goose with the split written through the standard library's
// each_split: the same flat list of strings, the same scan, so the row
// measures what the library's block-taking iteration costs against the
// hand-written loop (nothing, is the claim).

import rng;
import std;

let N = 400000;                          // BENCH_N
let PASSES = 4;

fn main() {
    // One text buffer of space-separated random words.
    var text: u8[>..] = [];
    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        let wl = 3 + xs_mod(r, 18);
        for j in wl {
            r = xs_next(r);
            text.push(('a' + xs_mod(r, 26)) as! u8);
        }
        text.push(' ');
    }
    // Split it into a flat list of strings; the trailing separator yields an
    // empty part, which the hand-written loop also skips.
    var words: u8[][>..] = [];
    each_split(text, ' ') { if it.len > 0 { words.push(it); } };
    // Scan the list repeatedly: length and first/last character of each word.
    var total = 0;
    for p in PASSES {
        for w in words { total += w.len + w[0] + w[w.len - 1]; }
    }
    print(words.len);
    print(total);
}
