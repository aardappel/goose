// Copying a value containing relative references is rejected: the offsets
// would be measured from the source location (§3.9, TODO scope tracking).
struct Cell { v: i64 = 0, link: i64&<u16>? }
fn main() {
    var pool: Cell[>..] = [];
    let a: Cell& = pool.push(Cell { v: 1 });
    pool.push(Cell { v: 2, link: &a.v });
    let copy = pool[0];
    print(copy.v);
}
