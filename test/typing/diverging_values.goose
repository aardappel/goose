// A construct that diverges on every path has no value of its own. As a
// function's tail, an assignment's right-hand side or a match in value
// position it is checked, folded and emitted like any other expression
// without a value; the constant `else if` is what the optimizer folds.
fn pick(x: i64) -> i64 {
    if x == 0 { return 1; } else if true { return 2; } else { return 3; }
}

fn choose(x: i64) -> i64 {
    var r = 0;
    r = if x == 0 { return 10; } else { return 20; };
    r
}

fn arms(x: i64) -> i64 {
    match x {
        0 => (return 100),
        _ => { return 200; },
    }
}

fn main() {
    print(pick(0), " ", pick(1));
    print(choose(0), " ", choose(5));
    print(arms(0), " ", arms(1));
}
