// error: a relative reference must point within the same root as its location
// error: this reference is rooted at xs
// Only a variable or parameter of type `T` holds the plain reference where T
// is a self-relative link (§3.9): the elements of a `T[1]` are links still,
// measured from slots of the local array, which a link into xs cannot be.
struct Cell { v: i64, links: (Cell&<u32>)[..4] }
fn keep_first<T>(xs: T[:]) {
    var held: T[1] = [xs[0]];
    xs[0] .= held[0];
}
fn main() {
    var cells: Cell[>..] = [];
    for i in 2 { cells.push(Cell { v: i, links: [] }); }
    cells[0].links.push(cells[1]);
    keep_first(cells[0].links);
}
