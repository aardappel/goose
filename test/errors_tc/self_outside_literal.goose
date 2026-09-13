// error: self is only valid as the initializer of a non-optional relative-reference field of the literal containing it
// `self` names the value a literal is constructing (§3.9), so it means
// nothing in an ordinary expression.
struct Cell { v: i64, link: Cell&<u16> }
fn main() {
    var pool: Cell[>..] = [];
    let a: Cell& = pool.push(Cell { v: 1, link: self });
    print(a.link.v + self);
}
