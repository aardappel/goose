// error: cannot assign through this path
// Rendering reads a self-relative link as a path would (§9.5): along a
// read-only path it is read-only, since it leads back into the value the
// `const` reference views (§3.9), so a `format` hook that writes is rejected.
struct Cell { v: i64 }
struct Inner { link: Cell&<u16>? }
struct Nest { cells: Cell[..4], inner: Inner }
fn format(out: u8[>..]&, v: Cell&) {
    v.v += 1;
    format(out, v.v);
}
fn show(t: const Nest&) { print(t.inner); }
fn main() {
    var t = Nest { cells: [], inner: Inner { link: null } };
    t.cells.push(Cell { v: 1 });
    t.inner.link .= t.cells[0];
    show(t);
    print(t.cells[0].v);
}
