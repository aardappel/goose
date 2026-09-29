// runtime-debug: a function declared in a global initializer names the
// initializer's locals in scope where it is declared, as one declared in a
// function names the function's (§7.5, §11.1). It takes them as hidden
// parameters, which gs_init_globals passes, wherever it stays a call: every
// call at -O0, and at -O2 the recursive ones and those handed to a recursive
// function.
struct PSlot { n: i64 = 0 }
struct Q { k: i64, xs: i64[>..] }
fn apply<F>() { F(); }
fn each<F>(n: i64) { for i in n { F(i); } }
recursive fn countdown<F>(k: i64) {
    if k > 0 { countdown(k - 1, F); }
    F(k);
}

let G1 = block {
    var n = 1;
    fn bump(k: i64) { n += k; }
    bump(2);
    bump(3);
    n
};
// Resizables, a pool, a frame object and a view, which travel as more than a
// pointer to the variable, and text printed from a function.
let G2 = block {
    var xs: i64[>..] = [1];
    reusable var pool: PSlot[>..] = [];
    let a = pool.alloc_index(PSlot { n: 1 });
    var q = Q { k: 1, xs: [] };
    var buf: u8[>..] = "hello";
    let v: u8[:] = buf[1..4];
    fn grow(k: i64) {
        xs.push(k);
        q.xs.push(k * 2);
    }
    fn slot() -> i64 {
        let b = pool.alloc_index(PSlot { n: xs.len });
        pool.free(a);
        b
    }
    fn show() { print("in G2: ", v, " ", xs, " ", q.xs); }
    grow(2);
    q.k += slot();
    grow(3);
    show();
    let c = pool.alloc_index(PSlot { n: 3 });
    c * 1000 + pool[1].n * 100 + q.k * 10 + v.len
};
// Recursion: a function calling itself, and two calling each other, the first
// declared calling the second, declared further on.
let G3 = block {
    var calls = 0;
    var memo: i64[>..] = [0, 1];
    recursive fn fib(k: i64) -> i64 {
        calls += 1;
        if k < memo.len { return memo[k]; }
        let r = fib(k - 1) + fib(k - 2);
        memo.push(r);
        r
    }
    recursive fn even(k: i64) -> bool {
        calls += 1;
        if k == 0 { true } else { odd(k - 1) }
    }
    fn odd(k: i64) -> bool {
        calls += 1;
        if k == 0 { false } else { even(k - 1) }
    }
    let f = fib(10);
    let e = if even(7) { 1 } else { 0 };
    f * 10000 + e * 1000 + calls
};
// Calls in a loop, a function declared in a loop's body naming the body's
// locals, and a loop in a function's body.
let G4 = block {
    var total = 0;
    fn add(k: i64) { total += k; }
    for i in 0..4 { add(i); }
    for i in 1..3 {
        let m = i * 10;
        fn scaled(k: i64) -> i64 { k * m + i }
        add(scaled(2));
    }
    fn sum_to(k: i64) -> i64 {
        var s = 0;
        for j in k { s += j + total; }
        s
    }
    total * 1000 + sum_to(3)
};
// Names resolve where the function is declared: its first call is made where
// another n is in scope, which it does not name.
let G5 = block {
    var n = 1;
    fn get() -> i64 { n }
    var t = 0;
    {
        var n = 50;
        t = get();
        n += 1;
        t = t * 100 + n;
    }
    t * 1000 + get()
};
// Handed on by name, to a recursive function too, and called from a block.
// again takes n and xs only to pass them on to countdown, which passes them
// on to step.
let G6 = block {
    var n = 0;
    var xs: i64[>..] = [];
    fn step(k: i64) {
        n = n * 10 + k;
        xs.push(k);
    }
    countdown(3, step);
    each(2, step);
    apply() { step(9); };
    recursive fn again(k: i64) {
        if k > 1 { again(k - 1); }
        countdown(k, step);
    }
    again(2);
    n * 100 + xs.len
};
// A function declared in one declared in the initializer names both's locals.
let G7 = block {
    var n = 2;
    fn outer(k: i64) -> i64 {
        var m = k;
        fn inner() {
            m *= n;
            n += 1;
        }
        inner();
        inner();
        m
    }
    outer(3) * 100 + outer(1) * 10 + n
};
// A reference result into the initializer's locals, from an early return.
let G8 = block {
    var a = 1;
    var b = 2;
    fn pick(c: bool) -> i64& {
        if c { return a; }
        b
    }
    let r .= pick(true);
    r += 10;
    let s .= pick(false);
    s += 20;
    a * 100 + b
};
// A `var` global's initializer.
var G9 = block {
    var n = G1;
    fn add() { n += G8; }
    add();
    n
};
// A deferred `let` of the initializer, which every exit of a function assigns.
let G10 = block {
    let n: i64;
    fn setn(d: bool) {
        if d {
            n = 10;
            return;
        }
        n = 20;
    }
    setn(G1 > 5);
    n
};
fn main() {
    G9 += 1;
    print(G1, " ", G2, " ", G3, " ", G4);
    print(G5, " ", G6, " ", G7, " ", G8, " ", G9, " ", G10);
}
