// runtime-debug: a discarded parameter is not a conditional return.
fn select_right(x: i32, y: i32) -> i32 {
    if x < y { x; }
    return y;
}

fn select_right_tail(x: i32, y: i32) -> i32 {
    if x < y { x; }
    y
}

// A function-body tail still supplies a value after a conditional return.
fn minimum_tail(x: i32, y: i32) -> i32 {
    if x < y { return x; }
    y
}

fn maximum_tail(x: i32, y: i32) -> i32 {
    if x < y { return y; }
    x
}

fn main() {
    var right: i32 = 1;
    var right_tail: i32 = 1;
    var low: i32 = 1;
    var high: i32 = 1;
    for i in 1 {
        right = select_right(select_right(right, 2), 3);
        right_tail = select_right_tail(select_right_tail(right_tail, 2), 3);
        low = minimum_tail(minimum_tail(low, 2), 3);
        high = maximum_tail(maximum_tail(high, 2), 3);
    }
    print(right, " ", right_tail, " ", low, " ", high);
}
