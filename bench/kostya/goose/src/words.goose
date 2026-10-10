// Etc::Words: count the words of a generated text in a hash map and report
// the most frequent one. The map is keyed by slices into the text, as the
// Rust version keys its map by &str.

namespace words;

import std;
import dictionary;
import bench;
import helper;

// The (word, count) tuple the reference's max_by_key gives. A slice
// variable keeps the scope of what it first viewed, and the words come out
// of the map, so the most frequent one is kept in a struct instead.
struct WordCount { word: const u8[:], count: i32 }

fn run(b: Bench&) -> u32 {
    let words = config_i64(b.name, "words");
    let word_len = config_i64(b.name, "word_len");
    let chars = "abcdefghijklmnopqrstuvwxyz";
    var text: u8[>..] = [];
    for i in words {
        let len = next_int(word_len) + next_int(3) + 3;
        for j in len { text.push(chars[next_int(chars.len)]); }
        if i < words - 1 { text.push(' '); }
    }
    var checksum: u32 = 0;
    measure(b) {
        var frequencies = dictionary<const u8[:], i32> {};
        each_word(text) { word => frequencies.update(word, 0) { it += 1; }; };
        var top = WordCount { word: "", count: 0 };
        frequencies.each() { word, count =>
            if count > top.count { top = WordCount { word, count }; }
        };
        checksum += (top.count as! u32) + checksum_str(top.word) + (frequencies.count as! u32);
    };
    return checksum;
}
