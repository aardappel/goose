// Word frequency counting: split a text into words, then count them in an
// open-addressed map (spec A.3). Two Goose properties are on trial here --
// the word list is one contiguous block of variable-size strings, and the map
// grows in place with slice keys pointing straight into the text, so no key
// is ever copied or allocated. The idiomatic C++ row copies every key into a
// std::string inside the map node.

import rng;

let N = 2000000;                         // BENCH_N
let MAXW = 20;

fn fnv(s: u8[:]) -> u64 {
    var h = 0xcbf29ce484222325;
    for c in s { h = (h ^ c) * 0x100000001b3; }
    h
}

struct Slot { key: u8[:], count: i32 }

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
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' { i++; }
        words.push(text[start..i]);
        while i < text.len && text[i] == ' ' { i++; }
    }
    // Count, in an open-addressed map whose keys are slices into the text.
    var nslots = 16;
    while nslots < vocab * 4 { nslots *= 2; }
    let mask = (nslots - 1) as u64;
    var slots: Slot[>..] = [];
    for s in nslots { slots.push(Slot { key: text[0..0], count: 0 }); }
    var distinct = 0;
    for w in words {
        let key: u8[:] = w[..];
        var idx = fnv(key) & mask;
        loop {
            if slots[idx].count == 0 {
                slots[idx].key = key;
                slots[idx].count = 1;
                distinct++;
                break;
            }
            if slots[idx].key == key { slots[idx].count = slots[idx].count + 1; break; }
            idx = (idx + 1) & mask;
        }
    }
    var total = 0;
    for &s in slots {
        if s.count > 0 { total += s.count * (s.key.len + s.key[0]); }
    }
    print(words.len);
    print(distinct);
    print(total);
}
