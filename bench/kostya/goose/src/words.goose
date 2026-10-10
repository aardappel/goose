// Etc::Words: count the words of a generated text in a hash map and report
// the most frequent one. The map is keyed by slices into the text, so no word
// is ever copied: a key is an address and a length.

namespace words;

import std;
import dictionary;
import bench;
import helper;

// A word, as a slice of the text, and how often it occurs.
struct WordCount { word: u8[:], count: i32 }

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "words");
    let word_len = config_i64(b.name, "word_len");
    let chars = "abcdefghijklmnopqrstuvwxyz";
    var text: u8[>..] = [];
    for i in n {
        let len = next_int(word_len) + next_int(3) + 3;
        for j in len { text.push(chars[next_int(chars.len)]); }
        if i < n - 1 { text.push(' '); }
    }
    var checksum: u32 = 0;
    measure(b) {
        var frequencies = dictionary<u8[:], i32> {};
        // `get`, and `insert` for a word not seen before, look a new word up
        // once, where `update` would look it up again after inserting it.
        each_word(text) { word =>
            let count = frequencies.get(word);
            if count { count += 1; } else { frequencies.insert(word, 1); }
        };
        var top = WordCount { word: text[0..0], count: 0 };
        frequencies.each() { word, count =>
            if count > top.count { top = WordCount { word, count }; }
        };
        checksum += (top.count as! u32) + checksum_str(top.word) + (frequencies.count as! u32);
    };
    return checksum;
}
