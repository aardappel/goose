// A relative link whose reference derives from a different pool fails at
// that instantiation, with the compile-time call chain (§3.9, §9.5).
struct Cell { v: int = 0, link: int&<u16>? }
fn store(pool: Cell[>..]&, r: int&) {
    pool.push(Cell { v: 1, link: r });
}
fn main() {
    var pool: Cell[>..] = [];
    var other: Cell[>..] = [];
    pool.push(Cell { v: 7 });
    other.push(Cell { v: 8 });
    var linked = 0;
    for &c in other {
        if linked == 0 {
            linked = 1;
            store(&pool, &c.v);
        }
    }
}
