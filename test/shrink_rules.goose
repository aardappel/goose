// Codegen execution test for the grow-only shrink rules (§5.1) as the
// checker applies them: a shrink is refused only where a reference into
// the array is in scope, or was stored into something in scope -- so a
// scratch buffer clears while unrelated containers of references exist, a
// grow-only array pops through a reference parameter, and a global grow-only
// array clears wherever nothing points into it.
import std;
import dictionary;

struct Entry { word: u8[:], count: i32 }

var log: u8[>..] = [];

// A helper shrinking through a reference: the caller's scope is checked
// at the call.
fn trim_zeros(n: i64[>..]&) {
    while n.len > 0 && n[n.len - 1] == 0 { n.pop(); }
}

fn note(s: u8[:]) {
    if log.len > 12 { log.clear(); }
    log.append(s);
}

fn main_shrink_rules() {
    let text = "the cat and the dog and the bird";
    var counts = dictionary<u8[:], i32> {};
    each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
    // `rows` holds slices into `text`, not into `line`, so the buffer
    // clears freely although rows is in scope.
    var rows: Entry[>..] = [];
    counts.each() { w, n => rows.push(Entry { word: w, count: n }); };
    sort(rows) { a, b => a.count > b.count || (a.count == b.count && compare(a.word, b.word) < 0) };
    var line: u8[>..] = [];
    for r in rows[..3] {
        line.clear();
        format(line, r.word, "=", r.count);
        print(line);
    }
    // Popping through a reference parameter.
    var xs: i64[>..] = [3, 1, 0, 0, 0];
    trim_zeros(xs);
    print(xs);
    // A grow-only global cleared inside a callee.
    note("abcdefg");
    note("hijklmn");
    note("op");
    print(log);
    // A holder whose contents point into the buffer is a copy of the
    // entries: its references are into `text`, so `line` still clears;
    // `words` holds slices into `line`, and is gone before the next clear.
    for i in 2 {
        var words: u8[:][>..] = [];
        line.clear();
        format(line, "alpha beta gamma ", i);
        each_split(line, ' ') { words.push(it); };
        print(words.len, " ", words[1]);
    }
}

fn main() { main_shrink_rules(); }
