// runtime-debug: stdlib/dictionary.goose with optional references as values,
// plain, `in pool` and into a local array. insert over an existing key
// stores the new reference and leaves the old pointee alone; get,
// get_or_insert, update's block and each's block receive the stored
// reference (loaded, where relative) and write through it to the pointee.
// A value that is null reads as an absent one does. Growth and deletion
// move the references, and a reference taken out of the dictionary points
// into the pool rather than into the slots, so it outlives later mutations.
import dictionary;

struct Cell { v: i64 }
var cells: Cell[>..] = [];

fn show(r: Cell?) -> i64 {
    if r { return r.v; }
    return -1;
}

fn plain() {
    var d = dictionary<i64, Cell?> {};
    print(d.insert(1, cells[1]), " ", d.insert(1, cells[2]), " ",
          d.insert(2, null));                                              // true false true
    print(cells[1].v, " ", show(d.get(1)), " ", show(d.get(2)), " ", d.contains(2), " ",
          show(d.get(3)));                                                 // 101 102 -1 true -1
    let g = d.get_or_insert(1, cells[9]);
    if g { g.v += 1000; }
    let h = d.get_or_insert(4, cells[4]);
    if h { h.v += 2000; }
    d.update(1, cells[5]) { if it { it.v += 1; } };
    d.update(5, cells[5]) { if it { it.v += 7; } };
    d.update(2, cells[6]) { assert(it == null); };
    print(cells[2].v, " ", cells[4].v, " ", cells[5].v, " ", cells[9].v, " ",
          d.count);                                                        // 1103 2104 112 109 4
    var total = 0;
    d.each() { k, c => if c { total += k * c.v; c.v += 1; } };
    print(total, " ", cells[2].v, " ", cells[4].v, " ", cells[5].v);       // 10079 1104 2105 113
    for i in 30 { d.insert(10 + i * 8, cells[i]); }
    var ok = d.count == 34;
    for i in 30 { if show(d.get(10 + i * 8)) != cells[i].v { ok = false; } }
    for i in 15 { d.remove(10 + i * 16); }
    for i in 30 {
        let r = d.get(10 + i * 8);
        if i % 2 == 0 { if r != null { ok = false; } } else if show(r) != cells[i].v { ok = false; }
    }
    print(ok, " ", d.count, " ", d.remove(2), " ", d.remove(2), " ",
          d.count);                                                        // true 19 true false 18
    let kept = d.get(1);
    d.insert(1, cells[3]);
    for i in 40 { d.insert(1000 + i, cells[i % 20]); }
    d.remove(4);
    d.clear();
    print(show(kept), " ", d.count, " ", d.contains(1));                   // 1104 0 false
}

fn relative() {
    var d = dictionary<i64, Cell&<u32 in cells>?> {};
    print(d.insert(1, cells[11]), " ", d.insert(1, cells[12]), " ",
          d.insert(2, null));                                              // true false true
    block { let r = d.get(1); if r { print(r.v, " ", index_of(cells, r)); } } // 112 12
    print(cells[11].v, " ", show(d.get(2)), " ", show(d.get(3)));          // 111 -1 -1
    let q = d.get_or_insert(6, cells[16]);
    if q { q.v += 3000; }
    d.update(1, null) { if it { it.v += 5; } };
    d.update(7, cells[17]) { if it { it.v += 9; } };
    print(cells[12].v, " ", cells[16].v, " ", cells[17].v, " ", d.count);  // 117 3116 126 4
    var n = 0;
    var seen: (Cell&<u16 in cells>?)[>..] = [];
    d.each() { k, c => if c { n += index_of(cells, c) * k; } seen.push(c); };
    print(n, " ", seen.len);                                               // 227 4
    for i in 30 { d.insert(100 + i, cells[i]); }
    for i in 10 { d.remove(100 + i * 3); }
    var sum = 0;
    for i in 30 { sum += show(d.get(100 + i)); }
    print(sum, " ", d.count);                                              // 8310 24
    var narrow = dictionary<i64, Cell&<u16 in cells>?> {};
    d.each() { k, c => narrow.insert(k, c); };
    var same = narrow.count == d.count;
    narrow.each() { k, c => if show(c) != show(d.get(k)) { same = false; } };
    let held = narrow.get(6);
    narrow.clear();
    print(same, " ", show(held));                                          // true 3116
}

fn local() {
    var mine: Cell[>..] = [];
    for i in 20 { mine.push(Cell { v: i }); }
    var d = dictionary<i64, Cell?> {};
    for i in 20 { d.insert(i, mine[19 - i]); }
    d.insert(3, mine[0]);
    d.each() { k, c => if c { c.v += 100; } };
    var s = 0;
    for x in mine { s += x.v; }
    print(s, " ", mine[0].v, " ", show(d.get(3)));                         // 2190 200 200
}

fn main() {
    while cells.len < 40 { cells.push(Cell { v: 100 + cells.len }); }
    plain();
    relative();
    local();
}
