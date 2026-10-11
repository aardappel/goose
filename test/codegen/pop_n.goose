// pop_n(n) drops the last n elements of limited, grow-shrink and grow-only
// arrays (§3.3): the length and, for a resizable array, the stack top move
// down by n elements at once, so what is pushed next lands where the popped
// elements were.

struct P { a: i32, b: i16 }

struct Stack { name: i64, items: i64[>..<] }

fn drop_through(q: P[>..<]&, n: i32) { q.pop_n(n); }

fn drop_tail(s: Stack&) { s.items.pop_n(2); }

fn main() {
    var lim: i64[..8] = [1, 2, 3, 4, 5, 6];
    lim.pop_n(2);
    print(lim);                          // [1, 2, 3, 4]
    let k: u8 = 3;
    lim.pop_n(k);
    print(lim, " ", lim.len);            // [1] 1
    lim.pop_n(0);
    lim.pop_n(lim.len);
    print(lim.len);                      // 0
    lim.push(9);
    print(lim);                          // [9]

    var gs: P[>..<] = [];
    for i in 6 { gs.push(P { a: i as i32, b: (i * 10) as i16 }); }
    var after = 7;                       // A local above the array's stack.
    drop_through(gs, 4);
    print(gs.len, " ", gs[1].b);         // 2 10
    gs.push(P { a: 42, b: 420 });
    print(gs[2].a, " ", gs[2].b, " ", after);   // 42 420 7
    pop_n(gs, 3);
    print(gs.len);                       // 0

    var go: i64[>..] = [];
    for i in 10 { go.push(i * i); }
    var n = 7;
    go.pop_n(n);
    print(go);                           // [0, 1, 4]
    go.push(100);
    print(go);                           // [0, 1, 4, 100]

    var s = Stack { name: 5, items: [10, 20, 30, 40] };
    drop_tail(s);
    s.items.push(50);
    print(s.name, " ", s.items);         // 5 [10, 20, 50]

    // The count runs before the shrink and may shrink the array itself:
    // pop_n takes the length that leaves.
    var c: i64[>..<] = [1, 2, 3, 4, 5];
    fn dropone() -> i64 { c.pop(); return 2; }
    c.pop_n(dropone());
    print(c);                            // [1, 2]
}
