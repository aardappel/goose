// A defer body runs when the block it is in ends, however it ends (§6.6):
// falling off the end, break, continue, return, a block's lexical return
// through a higher-order function, and the end of each loop iteration. An
// exit runs the defers written before it, latest first, after a return's
// value is built and before the block's storage is released.

fn fall_through() {
    defer print("fall 1");
    print("body");
    defer print("fall 2");
    {
        defer print("inner");
        print("inner body");
    }
    print("after inner");
}

fn early(n: i64) -> i64 {
    defer print("early ", n);
    if n > 0 { return n * 2; }
    print("negative");
    return -1;
}

// Evaluated at exit: the body sees variables as they are then.
fn at_exit() {
    var x = 1;
    defer print("x = ", x);
    x = 2;
}

// Only the defers an exit has run past.
fn ran_past(flag: bool) {
    defer print("first");
    if flag { return; }
    defer print("second");
}

fn loops() {
    for i in 4 {
        defer print("iter ", i);
        if i == 1 { continue; }
        if i == 3 { break; }
        print("body ", i);
    }
    var k = 0;
    let v = loop {
        k++;
        defer print("loop ", k);
        if k == 2 { break k * 10; }
    };
    print("v = ", v);
    var n = 0;
    while n < 3 {
        defer n++;        // The step, run at the end of every iteration.
        if n == 1 { continue; }
        print("while ", n);
    }
    print("n = ", n);
}

// A `block` exit with a value, and a branch's block.
fn blocks(p: i64) -> i64 {
    let w = block {
        defer print("block done");
        var s: u8[>..] = "hello";
        s.push(33);
        if s.len > 3 { break s.len; }
        0
    };
    if p > 0 {
        defer print("then done");
        print("then");
    } else {
        defer print("else done");
        print("else");
    }
    match p {
        0 => { defer print("arm 0 done"); print("arm 0"); },
        _ => { defer print("arm other done"); print("arm other"); },
    }
    return w;
}

// The value is built before the body runs: a defer that writes a local
// after `return` of a fixed value changes nothing the caller sees.
fn value_first() -> i64 {
    var n = 1;
    defer n = 100;
    return n;
}

// A defer may read the scope's locals, which are released after it.
fn reads_locals() {
    var items: i64[>..] = [1, 2, 3];
    let name: u8[] = "items";
    defer print(name, ": ", items.len, " ", items[2]);
    items.push(4);
}

// A defer in a loop body that the C compiler sees many times: one per copy.
fn sums(xs: i64[:]) -> i64 {
    var total = 0;
    for x in xs {
        defer total += x;
        if x % 2 == 0 { continue; }
    }
    return total;
}

fn each<F>(xs: i64[:]) { for x in xs { F(x); } }

// A block's lexical return through a higher-order function runs the
// block's own defers and those of the frame it leaves.
fn through_hof() -> i64 {
    var total = 0;
    defer print("through_hof done, total ", total);
    each([1, 2, 3]) {
        defer print("block ", it, " done");
        if it == 2 { return 99; }
        total += it;
    }
    return total;
}

// A defer written in a function value's body runs at the end of each run
// of the body, which is a block of the function the value runs in.
fn in_block() {
    each([1, 2]) {
        defer print("fv ", it);
        print("fv body ", it);
    }
}

// Several defers interleaved with storage: each body runs before the
// release of what was declared before it.
fn order() {
    var a: u8[>..] = "a";
    defer print("defer 1 sees ", a);
    var b: u8[>..] = "b";
    defer print("defer 2 sees ", a, b);
    a.push(97);
    b.push(98);
}

// A body's own locals and statements.
fn body_locals(n: i64) {
    defer {
        var acc: i64[>..] = [];
        for i in n { acc.push(i * i); }
        let label: u8[] = "squares";
        print(label, " ", acc);
    }
    print("computing ", n);
}

// A defer in an inlined callee runs where the inlined body exits.
inline fn tiny(n: i64) -> i64 {
    defer print("tiny ", n);
    if n == 0 { return 0; }
    return n + 1;
}

// The body may assign a variable of an outer scope; the assignment happens
// at the exit, so the outer code sees it after the block.
fn restores() {
    var depth = 0;
    {
        let saved = depth;
        depth = 5;
        defer depth = saved;
        print("depth ", depth);
    }
    print("depth ", depth);
}

fn main() {
    fall_through();
    print(early(5), " ", early(-5));
    at_exit();
    ran_past(true);
    ran_past(false);
    loops();
    print(blocks(1));
    print(blocks(0));
    print(value_first());
    reads_locals();
    print(sums([1, 2, 3, 4, 5]));
    print(through_hof());
    in_block();
    order();
    body_locals(3);
    print(tiny(0), " ", tiny(4));
    restores();
}
