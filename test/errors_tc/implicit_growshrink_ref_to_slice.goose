// error: storing a reference into items, which holds a grow-shrink array
// A grow-shrink array reference passed to a slice parameter is a view of the
// elements a shrink frees, exactly as an explicit `e.items[..]` is: it faces
// the store rule (§5.2) instead of counting as a read-back of the field slot
// the reference was loaded from, which passed that rule itself.
struct Item { x: i64 }
struct Engine { items: Item[>..<]& }
struct View { items: Item[:] }
fn make_view(items: Item[:]) -> View { return View { items }; }
fn finish(e: Engine&) -> i64 {
    var v = make_view(e.items);
    e.items.clear();
    e.items.push(Item { x: 999 });
    return v.items[0].x;
}
fn main() {
    var arr: Item[>..<] = [Item { x: 42 }];
    var e = Engine { items: arr };
    print(finish(e));
}
