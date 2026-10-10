// error: a is built where the result goes only where every return of pick gives it
// Two locals returned on different paths cannot both live at the result's
// destination: one would be copied, offsets and all (§3.9, §7.3).
struct Cell { id: i64, neighbors: (Cell&<u32>)[..4] }

fn pick(n: i64) -> Cell[>..] {
    var a: Cell[>..] = [];
    var b: Cell[>..] = [];
    a.push(Cell { id: 1, neighbors: [] });
    b.push(Cell { id: 2, neighbors: [] });
    if n > 0 { return a; }
    return b;
}

fn main() {
    print(pick(1).len);
}
