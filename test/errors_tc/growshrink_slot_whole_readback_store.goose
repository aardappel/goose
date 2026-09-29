// error: storing a reference into scratch, which holds a grow-shrink array
// A global may hold a reference to the whole of scratch (§5.2), and read
// back out of tops it still points at scratch: an element of it lies in
// scratch, and is never stored.
struct Cell { v: i64 }
var scratch: Cell[>..<] = [Cell { v: 1 }];
var tops: (Cell[>..<]&)[>..] = [];
var other: (Cell&)[>..] = [];
fn main() {
    tops.push(scratch);
    other.push(tops[0][0]);
    print(other.len);
}
