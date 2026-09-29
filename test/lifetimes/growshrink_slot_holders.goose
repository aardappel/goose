// A value holding references, read out of a field or an element, holds
// only references that were stored there, and none of those points into a
// grow-shrink array's elements (§5.2). So it is stored like any other holder
// whose references outlive the destination, its container still bounding
// them (§9.5), and no shrink of a grow-shrink array meets what it holds.
// Every use below was rejected as if what the holder holds might point into
// the grow-shrink array it was read out of: an element bound to a global,
// stored, bound first, copied by a for binder or out of a match's variant,
// passed to a function storing its parameter or storing it into a container
// of the caller's, returned out of a global array or as the parameter it was
// given, appended out of a slice, put into a literal, and popped; a
// reference read out of such a holder, returned by a function, stored; a
// reference read out of a field of an element, passed on to a function
// storing its parameter; a copy of an element passed as an argument while a
// call before the next one shrinks the array and refills the slot; and a
// holder parameter, or a reference read out of an element's field, passed
// on while a callee shrinks another grow-shrink array.

struct Cell { v: i64 }
struct Row { r: Cell&, c: Cell }
struct Wrap { row: Row }
struct Link { to: Cell& }
struct Pair { link: Link, spare: Cell }
enum Slot { Full { r: Cell&, c: Cell }, Empty }

var cells: Cell[>..] = [Cell { v: 10 }, Cell { v: 11 }, Cell { v: 12 }];
var rows: Row[>..<] = [Row { r: cells[0], c: Cell { v: 20 } },
                       Row { r: cells[1], c: Cell { v: 21 } }];
var pairs: Pair[>..<] = [Pair { link: Link { to: cells[2] }, spare: Cell { v: 30 } }];
var slots: Slot[>..<] = [];
var keep: Row[>..] = [];
var kept: Slot[>..] = [];
var wraps: Wrap[>..] = [];
var other: (Cell&)[>..] = [];
var first = rows[0];

fn store(r: Row) { keep.push(r); }
fn store_into(r: Row, into: Row[>..]&) { into.push(r); }
fn get(i: i64) -> Row { return rows[i]; }
fn pick(r: Row) -> Row { return r; }
fn ref_of(l: Link) -> Cell& { return l.to; }
fn keep_cell(x: Cell&) { other.push(x); }
fn via(e: Row&) { keep_cell(e.r); }
// Frees the slot of rows' last element and pushes a new one into it.
fn refill() -> i64 {
    rows.pop();
    rows.push(Row { r: cells[2], c: Cell { v: 99 } });
    return 0;
}
fn sum(r: Row, n: i64) -> i64 { return r.r.v + r.c.v + n; }
// Pops g's last element.
fn shrink(g: Cell[>..<]&) -> i64 {
    g.pop();
    return 0;
}
fn peek(r: Row, n: i64) -> i64 { return r.r.v + n; }
fn held_row(r: Row, g: Cell[>..<]&) -> i64 { return peek(r, shrink(g)); }
fn peek_cell(x: Cell&, n: i64) -> i64 { return x.v + n; }
fn held_cell(x: Cell&, g: Cell[>..<]&) -> i64 { return peek_cell(x, shrink(g)); }
fn held_field(e: Row&, g: Cell[>..<]&) -> i64 { return held_cell(e.r, g); }
fn held_calls() {
    var near: Cell[>..] = [Cell { v: 50 }, Cell { v: 51 }];
    var lines: Row[>..<] = [Row { r: near[0], c: Cell { v: 60 } },
                            Row { r: near[1], c: Cell { v: 61 } }];
    var spare: Cell[>..<] = [Cell { v: 40 }, Cell { v: 41 }, Cell { v: 42 }];
    let a = held_row(lines[0], spare);
    let b = held_field(lines[1], spare);
    print(a, " ", b, " ", spare.len);
    var popped: Row[>..] = [];
    popped.push(lines.pop());
    print(popped[0].r.v, " ", lines.len);
}

fn main() {
    keep.push(rows[0]);
    let h = rows[1];
    keep.push(h);
    for x in rows { keep.push(x); }
    slots.push(Slot.Full { r: cells[2], c: Cell { v: 22 } });
    match slots[0] {
        Full f => {
            kept.push(f);
        },
        Empty => {},
    }
    store(rows[1]);
    var mine: Row[>..] = [];
    store_into(rows[0], mine);
    keep.append(mine);
    keep.push(get(1));
    keep.push(pick(rows[0]));
    keep.append(rows[..]);
    wraps.push(Wrap { row: rows[1] });
    other.push(ref_of(pairs[0].link));
    via(rows[0]);
    print(first.r.v, " ", keep.len, " ", kept.len, " ", wraps[0].row.r.v);
    for k in keep { print(k.r.v, " ", k.c.v); }
    match kept[0] {
        Full f => print(f.r.v, " ", f.c.v),
        Empty => {}
    }
    for o in other { print(o.v); }
    print(sum(rows[1], refill()), " ", rows[1].c.v);
    held_calls();
}
