// error: storing a reference rooted at c, which does not outlive the destination
// A reference to a struct holding an i64 by value, stored beside them, may
// be where an i64 reference read back out of the holder points (§9.5).
struct Cell { v: i64 }
struct P { cell: Cell&, items: (i64&)[>..] }
struct S { refs: (i64&)[>..] }
fn prep(s: S&, pool: i64[>..]&) {
    var c = Cell { v: 1 };
    var p = P { cell: c, items: [] };
    p.items.push(pool[0]);
    s.refs.append(p.items);
}
fn main() {
    var pool: i64[>..] = [1];
    var s = S { refs: [] };
    prep(s, pool);
    print(s.refs.len);
}
