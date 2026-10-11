// error: storing a reference into c, which holds a grow-shrink array
// A reference argument whose pointee can lie in the elements of a
// grow-shrink array its root holds is one the callee never stores (§5.2).
struct Cell { v: i64 }
struct B { n: i64, cells: Cell[>..<] }
fn keep(c: Cell&) {
    var st: (Cell&)[>..] = [];
    st.push(c);
    print(st.len);
}
fn run(b: B&) { keep(b.cells[0]); }
fn main() {
    var b = B { n: 0, cells: [Cell { v: 1 }] };
    run(b);
}
