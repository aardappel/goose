// A writable slot is never given static data but a null or an empty slice
// (§9.5), so a reference read back out of one points into the one global that
// can hold its pointee, although a literal could supply a u8 as well.
struct Mark { at: u8& }
var text: u8[>..] = [];
var marks: Mark[>..] = [];

fn main() {
    text.push('a');
    text.push('b');
    marks.push(Mark { at: text[1] });
    print(text.index_of(marks[0].at), " ", marks[0].at);
}
