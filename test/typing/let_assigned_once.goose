// A `let` declared without a value is assigned once on every path: in each
// branch of an if or a match, at a loop's break, at a block's break and at
// its end, in a nested function before whichever exit leaves it, by a nested
// function or a function value called once, in each iteration of a loop that
// declares it, in each activation of a recursive function that declares it
// (§4.4).
enum Shape { Circle { r: i64 }, Square { s: i64 } }

fn once<F>() { F(); }

fn branches(c: bool) -> i64 {
    let x: i64;
    if c { x = 1; } else { x = 2; }
    return x;
}

fn matched(s: Shape) -> i64 {
    let x: i64;
    match s {
        Circle c => { x = c.r; },
        Square q => { x = q.s * q.s; }
    }
    return x;
}

fn declared_in_loop(n: i64) -> i64 {
    var sum = 0;
    for i in n {
        let y: i64;
        y = i * 2;
        sum += y;
    }
    return sum;
}

fn assigned_at_break(n: i64) -> i64 {
    let x: i64;
    var i = 0;
    loop {
        if i * i > n {
            x = i;
            break;
        }
        i += 1;
    }
    return x;
}

fn block_exits(c: bool) -> i64 {
    let x: i64;
    block {
        if c {
            x = 10;
            break;
        }
        x = 20;
    }
    return x;
}

fn nested_init(c: bool) -> i64 {
    let x: i64;
    fn init(v: i64) { x = v; }
    if c { init(3); } else { init(4); }
    return x;
}

fn block_once() -> i64 {
    let x: i64;
    once() { x = 7; };
    return x;
}

fn guarded(c: bool) -> i64 {
    let x: i64;
    guard c else { return -1; }
    x = 5;
    return x;
}

fn after_loop(n: i64) -> i64 {
    let x: i64;
    var i = 0;
    while i < n { i += 1; }
    x = i;
    return x;
}

fn bound_once(c: bool) -> i64 {
    var a = 1;
    var b = 2;
    let r: i64?;
    if c { r .= &a; } else { r .= &b; }
    if r { return r; }
    return 0;
}

recursive fn per_activation(n: i64) -> i64 {
    let x: i64;
    x = n;
    if n > 0 { return x + per_activation(n - 1); }
    return x;
}

fn far_exit(c: bool) -> i64 {
    let x: i64;
    fn setx(d: bool) {
        fn bail(e: bool) {
            if e {
                x = 1;
                return from setx;
            }
        }
        bail(d);
        x = 2;
    }
    setx(c);
    return x;
}

fn main() {
    print(branches(true), " ", branches(false));
    print(matched(Shape.Circle { r: 3 }), " ", matched(Shape.Square { s: 3 }));
    print(declared_in_loop(4));
    print(assigned_at_break(10));
    print(block_exits(true), " ", block_exits(false));
    print(nested_init(true), " ", nested_init(false));
    print(block_once());
    print(guarded(true), " ", guarded(false));
    print(after_loop(3));
    print(bound_once(true), " ", bound_once(false));
    print(per_activation(3));
    print(far_exit(true), " ", far_exit(false));
}
