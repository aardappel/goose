// error: may point into buf or log
// Two globals can hold a u8, so index_of cannot tell which one a reference
// read back out of `marks` points into. Static data is not among the
// candidates: a writable slot is never given a literal (§9.5).
struct Mark { at: u8& }
var buf: u8[>..] = [];
var log: u8[>..] = [];
var marks: Mark[>..] = [];

fn main() {
    buf.push('a');
    marks.push(Mark { at: buf[0] });
    print(buf.index_of(marks[0].at), log.len);
}
