// A value known to be null stores into an optional relative slot wherever the
// slot is (§3.9), even where a reference of its type could point into either
// of two global pools. The literal is one; so is a variable bound to nothing
// but null so far, read where every earlier binding has been checked (§9.5),
// in a nested function or a block as well, a parameter given null, a call
// returning only null, default<T>(), and a global `let` of null. In a merged
// value null names no array (§9.2), so it leaves the other branch to say
// where the link points.

struct Cell { key: i64, next: Cell&<u16 in cells>? }
var cells: Cell[>..] = [];
reusable var spare: Cell[>..] = [];
struct Slot { at: Cell&<u32 in cells>?, n: i64 }
struct Link { key: i64, next: Link&<u16>? }
var bytes: u8[>..] = [];
struct Byte { b: const u8&<u16 in bytes>? }

let nowhere: Cell&? = null;

fn none() -> Cell&? { return null; }

fn none_local() -> Cell&? {
    let n: Cell&? = null;
    return n;
}

fn slot_of(p: Cell&?, n: i64) -> Slot { return Slot { at: p, n: n }; }

fn apply<F>() { F(); }

fn show_cell(c: Cell&) {
    let nx = c.next;
    if nx { print(c.key, " -> ", nx.key); } else { print(c.key, " -> null"); }
}

fn show_slot(s: Slot&) {
    let at = s.at;
    if at { print("slot ", s.n, ": ", at.key); } else { print("slot ", s.n, ": null"); }
}

fn main() {
    spare.push(Cell { key: -1 });
    let none_let: Cell&? = null;
    var none_var: Cell&? = null;
    none_var .= null;
    cells.push(Cell { key: 0, next: none_let });
    cells.push(Cell { key: 1, next: none_var });
    cells.push(Cell { key: 2, next: nowhere });
    cells.push(Cell { key: 3, next: none() });
    cells.push(Cell { key: 4, next: none_local() });
    cells.push(Cell { key: 5, next: default<Cell&?>() });
    let merged: Cell&? = if cells.len > 3 { cells[0] } else { none_let };
    cells.push(Cell { key: 6, next: merged });
    let merged_default: Cell&? = if cells.len > 100 { default<Cell&?>() } else { cells[2] };
    cells.push(Cell { key: 7, next: merged_default });
    cells[3].next .= none_let;
    cells[4].next .= &cells[5];
    for i in cells.len { show_cell(cells[i]); }

    var slots: Slot[>..] = [];
    slots.push(slot_of(none_let, 0));
    slots.push(slot_of(null, 1));
    slots.push(Slot { at: none_var, n: 2 });
    // Null throughout the loop, which never binds it; bound only after it.
    var last: Cell&? = null;
    for i in 3 { slots.push(Slot { at: last, n: 10 + i }); }
    last .= &cells[6];
    slots.push(Slot { at: last, n: 20 });
    // Null in the branch that reads it; the other one binds it elsewhere.
    var later: Cell&? = null;
    if spare.len > 0 {
        slots.push(Slot { at: later, n: 30 });
    } else {
        later .= &spare[0];
    }
    // Null where it is read; bound into the other pool only afterwards.
    var tail: Cell&? = null;
    slots.push(Slot { at: tail, n: 40 });
    tail .= &spare[0];
    if tail { print("tail ", tail.key); }
    // Null where a nested function or a block reads it; a call after the
    // rebind into cells is checked for that (§7.5).
    var parked: Cell&? = null;
    fn park(n: i64) { slots.push(Slot { at: parked, n: n }); }
    park(50);
    apply() { slots.push(Slot { at: parked, n: 51 }); };
    parked .= &cells[1];
    park(52);
    for i in slots.len { show_slot(slots[i]); }

    var links: Link[>..] = [];
    let nolink: Link&? = null;
    links.push(Link { key: 0, next: nolink });
    links.push(Link { key: 1, next: null });
    links[0].next .= &links[1];
    links[1].next .= nolink;
    for i in links.len {
        let nx = links[i].next;
        if nx { print("link ", links[i].key, " -> ", nx.key); } else { print("link ", links[i].key, " -> null"); }
    }

    bytes.push(7);
    let nobyte: const u8&? = null;
    let bs = [Byte { b: nobyte }, Byte { b: default<const u8&?>() }, Byte { b: bytes[0] }];
    for i in bs.len {
        let b = bs[i].b;
        if b { print("byte ", i, ": ", b); } else { print("byte ", i, ": null"); }
    }
}
