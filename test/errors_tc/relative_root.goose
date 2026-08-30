// A relative reference must derive from the same root as its location (§3.9).
struct Cell { link: int&<u16>? }
var pool: Cell[>..] = [];
var other = 5;
fn main() {
    pool.push(Cell { link: &other });
}
