// A reference or slice popped out of an array leaves as itself: what a
// temporary holds is not rooted at the temporary (§9.2), so it points where
// the element did and is as writable as the element's slot (§9.5). It binds
// to a variable, writes through, stays an element of its array for
// index_of, and returns through a reference parameter's array.
struct Cell { id: i64, neighbors: (Cell&<u32>)[..4] }

fn take(s: i64&[>..<]&) -> i64& { return s.pop(); }

fn words(text: const u8[:]) -> i64 {
    var stack: (const u8[:])[>..<] = [];
    stack.push(text[0..5]);
    stack.push(text[6..]);
    let w = stack.pop();
    let v: const u8[:] = stack.pop();
    print(w, " ", v);
    return w.len + v.len;
}

fn main() {
    print(words("hello world"));
    var xs: i64[4] = [1, 2, 3, 4];
    var stack: i64&[>..<] = [];
    stack.push(xs[1]);
    stack.push(xs[2]);
    let r: i64& = stack.pop();
    r = 30;
    let q .= stack.pop();
    q = 20;
    print(xs);
    var cells: Cell[>..] = [];
    for i in 4 { cells.push(Cell { id: i, neighbors: [] }); }
    cells[0].neighbors.push(cells[2]);
    cells[0].neighbors.push(cells[3]);
    let n .= cells[0].neighbors.pop();
    print(n.id, " ", cells.index_of(n));
    cells[1].neighbors.push(n);
    print(cells[1].neighbors[0].id);
    var ys: i64[3] = [7, 8, 9];
    var st: i64&[>..<] = [];
    st.push(ys[0]);
    st.push(ys[2]);
    let t .= take(st);
    t += 100;
    print(ys, " ", st.len);
}
