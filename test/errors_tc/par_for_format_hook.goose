// error: cannot assign through this path
// str renders what a holder from outside the body refers to by handing it
// to a format overload, which writes it no more than the body may
// (docs/design/child_threads.md §4.3 R1).
struct Item { n: i64 }
struct Holder { r: Item& }
fn format(out: u8[>..]&, item: Item&) { item.n += 1; out.append("i"); }
fn main() {
    var it = Item { n: 0 };
    let h = Holder { r: it };
    par for k in 100000 { let s = str(h); }
    print(it.n);
}
