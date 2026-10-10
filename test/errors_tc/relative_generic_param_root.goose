// error: a relative reference must point within the same root as its location
// error: this reference is rooted at v
// fill's `v: T` over an array of self-relative links is the plain reference
// (§3.9), which takes a reference into another array; storing it into the
// links is what is rejected, since the offsets would span the two arrays.
import std;
struct Cell { v: i64, links: (Cell&<u32>)[..4] }
fn main() {
    var xs: Cell[>..] = [];
    var ys: Cell[>..] = [];
    for i in 2 {
        xs.push(Cell { v: i, links: [] });
        ys.push(Cell { v: i + 10, links: [] });
    }
    xs[0].links.push(xs[1]);
    fill(xs[0].links, &ys[1]);
}
