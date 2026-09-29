// runtime-debug: a function value written in a global initializer names that
// initializer's locals, which the functions it is handed to take as hidden
// parameters, as they would a function's locals (§7.5, §7.6): every such
// function at -O0, and at -O2 the recursive and the dispatched ones, which
// stay calls there.
struct PSlot { n: i64 = 0 }
struct Q { k: i64, xs: i64[>..] }
enum M { A { n: i64 }, B }
enum Shape { Circle { r: i64 }, Square { s: i64 } }
fn apply<F>() { F(); }
fn value<F>() -> i64 { F() }
fn twice<F>() -> i64 { F() + F() }
fn each<F>(n: i64) { for i in n { F(i); } }
fn outer<F>() { apply() { F(); F(); } }
recursive fn countdown<F>(k: i64) {
    if k > 0 { countdown(k - 1, F); }
    F(k);
}
fn visit<F>(c: Shape.Circle) { F(c.r); }
fn visit<F>(q: Shape.Square) { F(q.s * q.s); }
fn search<F>(n: i64) -> i64 { for i in n { F(i); } -1 }
let shapes: Shape[2] = [Shape.Circle { r: 3 }, Shape.Square { s: 4 }];
var ys: i64[3] = [1, 2, 3];
var mv: M.. = M.A { n: 6 };

let G1 = block {
    var n = 1;
    apply() { n += 1; };
    n
};
// A loop around the call, whose variable the block names too.
let G2 = block {
    var n = 1;
    for i in 0..3 { apply() { n += i; }; }
    n
};
// A parameter, a value, two calls, a block handed on in a block.
let G3 = block {
    var n = 0;
    each(5) { i => n += i * i; };
    n
};
let G4 = block {
    var n = 1;
    twice() { n += 1; n }
};
let G5 = block {
    var n = 1;
    outer() { n *= 3; };
    n
};
// Resizables, a pool, a frame object and a view, which travel as more than a
// pointer to the variable, and text printed from the block.
let G6 = block {
    var xs: i64[>..] = [1];
    reusable var pool: PSlot[>..] = [];
    let a = pool.alloc_index(PSlot { n: 1 });
    var q = Q { k: 1, xs: [] };
    var buf: u8[>..] = "hello";
    let v: u8[:] = buf[1..4];
    apply() {
        xs.push(2);
        let b = pool.alloc_index(PSlot { n: xs.len });
        pool.free(a);
        q.xs.push(4);
        q.k += b;
        print("in G6: ", v, " ", xs);
    };
    let c = pool.alloc_index(PSlot { n: 3 });
    c * 10000 + pool[1].n * 1000 + q.k * 100 + q.xs[0] * 10 + value() { v.len }
};
// Recursive and dispatched: calls at every optimization level.
let G7 = block {
    var n = 0;
    countdown(4) { n = n * 10 + it; };
    n
};
let G8 = block {
    var n = 0;
    for sh in shapes { visit(sh) { n = n * 100 + it; }; }
    n
};
// A long-distance return out of the function the block is handed to. The block
// only reads n, which the write keeps from folding to its value.
let G9 = block {
    var n = 1;
    n += 1;
    search(5) { if it + n == 5 { return it * 10 from search; } }
};
// Two locals of one name, and variables bound by reference.
let G10 = block {
    var n = 1;
    var t = 0;
    apply() { n += 1; };
    {
        var n = 20;
        apply() { n += 2; t = n; };
    }
    n * 100 + t
};
let G11 = block {
    var t = 0;
    for &y in ys { apply() { y *= 10; t += y; }; }
    match mv { A &a => { apply() { a.n += t; }; }, B => {} }
    t
};
// A `var` global's initializer.
var G12 = block {
    var n = G1;
    apply() { n += G11; };
    n
};
fn main() {
    // The same functions with a block of main's.
    var m = 1000;
    apply() { m += G1; };
    G12 += 1;
    print(G1, " ", G2, " ", G3, " ", G4, " ", G5);
    print(G6);
    print(G7, " ", G8, " ", G9, " ", G10);
    print(G11, " ", ys, " ", mv, " ", G12, " ", m);
}
