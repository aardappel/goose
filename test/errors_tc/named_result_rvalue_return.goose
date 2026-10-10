// error: a is built where the result goes only where every return of maybe gives it
// A return of anything else leaves the function with no named result, so a
// is copied where it is returned (§3.9, §7.3).
struct Cell { id: i64, neighbors: (Cell&<u32>)[..4] }

fn maybe(n: i64) -> Cell[>..] {
    var a: Cell[>..] = [];
    if n < 0 { return []; }
    a.push(Cell { id: 1, neighbors: [] });
    return a;
}

fn main() {
    print(maybe(1).len);
}
