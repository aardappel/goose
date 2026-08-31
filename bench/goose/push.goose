// Growth while holding element pointers. Goose grows by bumping a stack
// pointer, so memory never moves and the references handed out by `push`
// stay valid for the array's whole life. C++ has to pick a workaround:
// reserve a correct upper bound, keep indices instead of pointers, or pay
// for a chunked container. This measures the cost of that choice.

import rng;

let N = 4000000;                         // BENCH_N

struct Item { key: i32, val: i32 }

fn main() {
    var items: Item[>..] = [];
    var marks: Item&[>..] = [];
    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        let e: Item& = items.push(Item { key: xs_mod(r, 1000000) as i32,
                                         val: (i % 97) as i32 });
        if i % 64 == 0 { marks.push(e); }    // A pointer, kept across all later growth.
    }
    var total = 0;
    for it in items { total += it.key; }
    var mt = 0;
    for m in marks { mt += m.val; }
    print(items.len);
    print(marks.len);
    print(total);
    print(mt);
}
