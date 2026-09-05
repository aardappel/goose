// Word frequencies of a text file: the program every language's tutorial
// writes, with one Goose twist. The counting map is keyed by slices into
// the text itself, so no word is ever copied -- the text is read once into
// one buffer, lowercased in place, and every key, every table row and the
// final report point into it.
//
// Build and run (from this directory):
//   goose -o word_freq.c 15_word_freq.goose && cl word_freq.c && word_freq [file]

import std;
import dictionary;
import os;

struct Entry { word: u8[:], count: i32 }

fn is_letter(c: u8) -> bool { (c >= 'a' && c <= 'z') || c == '\'' }

fn main() {
    // The file to read: the first argument, or the bundled text.
    var path: u8[>..] = [];
    if arg_count() > 1 { arg(1, path); } else { path.append("data/alice.txt"); }
    var text: u8[>..] = [];
    guard read_file(path, text) else { print("cannot read ", path); exit(1); }
    to_lower(text);

    // Count. `update` inserts 0 on a first sighting and then runs the block
    // on the stored value, so one lookup does both. A key is a slice, an
    // address and a length into `text`: the dictionary stores no strings.
    var counts = dictionary<u8[:], i32> {};
    var total = 0;
    var longest = text[0..0];
    var i = 0;
    while i < text.len {
        while i < text.len && !is_letter(text[i]) { i++; }
        let start = i;
        while i < text.len && is_letter(text[i]) { i++; }
        guard i > start else { continue; }
        let word = text[start..i];
        counts.update(word, 0) { it += 1; };
        total++;
        if word.len > longest.len { longest = word; }
    }

    // Into an array of rows, sorted by count and then alphabetically. A row
    // is a slice and a count, so sorting moves no text either.
    var rows: Entry[>..] = [];
    counts.each() { w, n => rows.push(Entry { word: w, count: n }); };
    sort(rows) { a, b => a.count > b.count || (a.count == b.count && compare(a.word, b.word) < 0) };

    print(path, ": ", text.len, " bytes, ", total, " words, ", rows.len, " distinct");
    print("longest word: ", longest);
    var once = 0;
    for r in rows { if r.count == 1 { once++; } }
    print(once, " words appear once");
    print("");
    print("  count  word");
    for r in rows[..15] {
        var line: u8[>..] = [];          // fresh per row; the closing brace frees it
        format_int(line, r.count, 10, 7, ' ');
        format(line, "  ", r.word);
        print(line);
    }
}
