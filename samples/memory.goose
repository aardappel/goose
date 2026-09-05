// Where values live, and what that buys. Goose has no allocator: every
// dynamic value is owned by a variable and lives on a data stack, growth is
// a pointer bump, and the only "free" is a scope ending. This program walks
// through the consequences one at a time, with the numbers to show for it.
//
// Build and run (from this directory):
//   goose -o memory.c memory.goose && cl memory.c && memory

import std;

struct Item { id: i32, weight: f32 }

// A limited array is fixed-size storage with a length: capacity inline,
// no pointer, so a struct holding one is still one flat value.
struct Row { label: u8[..12], cells: i32[..8] }

fn reset(buf: u8[>..]&) { buf.clear(); }

fn main() {
    // 1. A grow-only array never moves its elements. A reference taken
    //    early stays valid however much is pushed afterwards, which is what
    //    a vector<T> or Vec<T> cannot promise: they reallocate.
    var items: Item[>..] = [];
    let first: Item& = items.push(Item { 1, 0.5 });
    for i in 2..100001 { items.push(Item { i as i32, i as f32 * 0.5 }); }
    first.weight = 99.5;                 // through the reference, 100000 pushes later
    print(items.len, " items, first weight ", items[0].weight);

    // 2. Scope exit is the free. A buffer declared in a loop body is fresh
    //    each iteration and gone at the closing brace: the data stack's top
    //    is reset to where it was, no destructor runs, no list is walked.
    for round in 3 {
        var scratch: u8[>..] = [];
        for i in 10000 { scratch.push((i % 256) as! u8); }
        print("round ", round, ": ", scratch.len, " bytes of scratch");
    }

    // 3. Or keep one buffer and clear it. A grow-only array may shrink at
    //    a point where nothing in scope refers into it -- the compiler
    //    knows every place a reference into it was stored, and names the
    //    holder if one is live -- so a reused line buffer costs nothing
    //    per line, here through a helper that clears its argument.
    var line: u8[>..] = [];
    for i in 3 {
        reset(line);
        format(line, "line ", i + 1, " of 3");
        print(line);
    }

    // 4. Nested containers stay flat. An array of strings is one block:
    //    lengths and bytes back to back, no pointer per string. Dropping it
    //    is the same pointer reset as dropping an array of integers.
    var words: u8[][>..] = [];
    for i in 5 { words.push(str("word", i)); }   // built straight into the element
    var bytes = 0;
    for w in words { bytes += w.len; }
    print(words.len, " words, ", bytes, " bytes of text, ", words.len * 4 + bytes,
          " bytes in total (4-byte lengths)");

    // 5. Fixed-size values are copied on assignment; a resizable is not
    //    copied unless the program says so, because that copy is O(n).
    var a: i64[>..] = [1, 2, 3];
    var b = copy(a);
    b.push(4);
    var item2 = items[1];                // a copy: Item is fixed-size
    item2.weight = 0.0;
    print(a, " ", b, " ", items[1].weight);

    // 6. A grow-shrink array pops as well: the natural stack, queue and
    //    scratch heap. References into it live in variables only, so a pop
    //    can never leave a stored reference behind.
    var stack: i64[>..<] = [];
    for i in 5 { stack.push(i * 10); }
    var popped = 0;
    while stack.len > 2 { popped += stack.pop(); }
    print(stack, " popped ", popped);

    // 7. Limited arrays live inline: a struct with two of them is one flat
    //    value with a fixed size, so an array of them is indexable and every
    //    row is next to the previous one in memory.
    var rows: Row[>..] = [];
    for r in 3 {
        var row = Row { label: "row", cells: [] };
        format(row.label, r);
        for c in r + 1 { row.cells.push((r * 10 + c) as i32); }
        rows.push(row);
    }
    for row in rows { print(row.label, ": ", row.cells, " (", row.cells.len, "/", row.cells.cap, ")"); }

    // 8. When lifetimes really are not nested -- a cache, a mutable graph --
    //    a `reusable` pool keeps a freelist of slots. Every slot stays a
    //    valid Item forever, so a stale reference can read a wrong value but
    //    never corrupt memory; that is the whole extent of unsafety.
    reusable var pool: Item[>..] = [];
    let s0 = pool.alloc_index(Item { 10, 1.0 });
    let s1 = pool.alloc_index(Item { 11, 1.0 });
    pool.free(s0);
    let s2 = pool.alloc_index(Item { 12, 2.0 });   // takes slot s0 back
    print("slots ", s0, " ", s1, " ", s2, ", pool length ", pool.len);
    let node: Item& = pool.alloc_ref(Item { 13, 3.0 });
    pool.free(pool.index_of(node));                // a reference turned back into its slot
    print("pool length ", pool.len);
}
