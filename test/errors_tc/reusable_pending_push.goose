// error: reusable[] applies to grow-only arrays of fixed-size elements (§5.4), and this makes runs a Line[>..]
// Whatever first goes into a pool declared with `= []` gives it its element
// type (§4.2), a push into a global one in a function included, and a slice
// pool's elements are fixed-size as a slot pool's are (§5.4).
struct Line { text: u8[] }

reusable[] var runs = [];

fn add(t: u8[:]) {
    runs.push(Line { text: t });
}

fn main() {
    add("abc");
    print(runs.alloc_slice(2).len);
}
