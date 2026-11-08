// error: cannot assign through this path
// As par_for_format_hook.goose, rendering a copy of the holder made in the
// body (docs/design/child_threads.md §4.3 R1).
struct Item { n: i64 }
struct Holder { r: Item& }
fn format(out: u8[>..]&, item: Item&) { item.n += 1; out.append("i"); }
fn main() {
    var it = Item { n: 0 };
    let h = Holder { r: it };
    par for k in 100000 { var copy = h; let s = str(copy); }
    print(it.n);
}
