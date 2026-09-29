// error: storing a reference into scratch, which holds a grow-shrink array
// A holder read out of tops may be stored into keep (§5.2), and the
// reference it holds, read back out of keep, still points at the whole of
// scratch: an element of scratch lies in a grow-shrink array, and is never
// stored.
struct Cell { v: i64 }
struct Top { a: Cell[>..<]& }
var scratch: Cell[>..<] = [Cell { v: 1 }];
var tops: Top[>..<] = [];
var keep: Top[>..] = [];
var other: (Cell&)[>..] = [];
fn main() {
    tops.push(Top { a: scratch });
    keep.push(tops[0]);
    other.push(keep[0].a[0]);
    print(other.len);
}
