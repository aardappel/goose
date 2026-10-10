// error: a relative reference must point within the same root as its location
// error: this reference is rooted at src
// A `T` local over an array of self-relative links holds the plain reference
// (§3.9), and storing it into an element still needs it to point within that
// element's root: this one was read out of another array.
struct Cell { v: i64, links: (Cell&<u32>)[..4] }
fn move_first<T>(dst: T[:], src: T[:]) {
    let t: T = src[0];
    dst[0] .= t;
}
fn main() {
    var xs: Cell[>..] = [];
    var ys: Cell[>..] = [];
    for i in 2 {
        xs.push(Cell { v: i, links: [] });
        ys.push(Cell { v: i + 10, links: [] });
    }
    xs[0].links.push(xs[1]);
    ys[0].links.push(ys[1]);
    move_first(xs[0].links, ys[0].links);
}
