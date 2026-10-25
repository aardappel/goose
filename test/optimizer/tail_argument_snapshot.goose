// An earlier by-value argument keeps its value when a later argument
// changes the parameter. Keep base-case inlining out of this test.
recursive fn keep_first(var x: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return x; }
    return keep_first(x, { x += 1; n - 1 });
}

fn bump(x: i64&) { x += 1; }
recursive fn keep_aliased(x: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return x; }
    return keep_aliased(x, { bump(x); n - 1 });
}

// The earlier value also needs a temporary when it is an expression.
recursive fn keep_computed(var x: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return x; }
    return keep_computed(x + 2, { x += 1; n - 1 });
}

recursive fn keep_reference(var p: i64&, q: i64&, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return p; }
    return keep_reference(p, q, { p .= q; n - 1 });
}

// An aggregate cannot be carried by this pass. Its earlier copy must stay
// independent of a later write through a reference to one of its fields.
struct Box { x: i64 }
recursive fn keep_box(b: Box, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return b.x; }
    return keep_box(b, { bump(b.x); n - 1 });
}

fn main() {
    print(keep_first(7, 0));
    print(keep_first(7, 3));
    print(keep_aliased(7, 3));
    print(keep_computed(7, 3));
    var x = 7;
    var y = 10;
    print(keep_reference(x, y, 3));
    print(keep_box(Box { x: 7 }, 3));
}
