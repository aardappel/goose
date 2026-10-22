// A pool declared with `= []` takes its element type from what first goes
// into it, as any `var x = []` does (§4.2): an allocation, a push or an
// append, which give it the fixed-size element a pool needs (§5.4), as
// an annotation would.

struct P { x: i32, y: i32 }

fn main() {
    reusable var pool = [];
    let i = pool.alloc_index(5);
    let j = pool.alloc_index(6);
    pool.free(i);
    let k = pool.alloc_index(7);
    print(i, " ", j, " ", k, " ", pool);                      // 0 1 0 [7, 6]
    reusable var pts = [];
    pts.push(P { x: 1, y: 2 });
    let r .= pts.alloc_ref(P { x: 3, y: 4 });
    pts.free(0);
    print(pts.alloc_index(P { x: 5, y: 6 }), " ", r.y, " ", pts);   // 0 4 [P { 5, 6 }, P { 3, 4 }]
    reusable[] var runs = [];
    runs.append([1, 2]);
    let s = runs.alloc_slice(2);
    s[1] = 9;
    runs.free_slice(s);
    print(runs.alloc_slice(1).len, " ", runs);                // 1 [1, 2, 0, 9]
}
