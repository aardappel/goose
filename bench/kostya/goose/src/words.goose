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

// A growable array can only be a struct's last field, so the text follows
// checksum_val.
struct Words { words: i64, word_len: i64, checksum_val: u32, text: u8[>..] }

fn new_words() -> Words {
    Words {
        words: config_i64("Etc::Words", "words"),
        word_len: config_i64("Etc::Words", "word_len"),
        checksum_val: 0,
        text: [],
    }
}

fn prepare(s: Words&) {
    let chars = "abcdefghijklmnopqrstuvwxyz";
    for i in s.words {
        let word_len = next_int(s.word_len) + next_int(3) + 3;
        for j in word_len { s.text.push(chars[next_int(chars.len)]); }
        if i < s.words - 1 { s.text.push(' '); }
    }
}

fn run(s: Words&, iteration_id: i64) {
    var frequencies = dictionary<const u8[:], i32> {};
    each_word(s.text) { word => frequencies.update(word, 0) { it += 1; }; };
    var top = WordCount { word: "", count: 0 };
    frequencies.each() { word, count =>
        if count > top.count { top = WordCount { word, count }; }
    };
    s.checksum_val += (top.count as! u32) + checksum_str(top.word) + (frequencies.count as! u32);
}

fn checksum(s: Words&) -> u32 { s.checksum_val }
