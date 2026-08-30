// A list of strings, the shape from spec A.1. In Goose the whole list is one
// contiguous block -- `len | (len | bytes)*` -- built with no allocations at
// all. In C++ it is one allocation for the vector plus one per string that
// does not fit the small-string buffer, each with its own malloc header, and
// the elements are 32 bytes of pointer/size/capacity before any characters.
// Word lengths here run 3..20, straddling the SSO boundary on purpose.

import rng;

let N = 400000;                          // BENCH_N
let PASSES = 4;

fn main() {
    // One text buffer of space-separated random words.
    var text: u8[>..] = [];
    var r = 12345;
    for i in N {
        r = xs_next(r);
        let wl = 3 + xs_mod(r, 18);
        for j in wl {
            r = xs_next(r);
            text.push(('a' + xs_mod(r, 26)) as! u8);
        }
        text.push(' ');
    }
    // Split it into a flat list of strings.
    var words: u8[][>..] = [];
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' { i++; }
        words.push(text[start..i]);
        while i < text.len && text[i] == ' ' { i++; }
    }
    // Scan the list repeatedly: length and first/last character of each word.
    var total = 0;
    for p in PASSES {
        for &w in words { total += w.len + w[0] + w[w.len - 1]; }
    }
    print(words.len);
    print(total);
}
