// error: returning a reference rooted in xs, which dies with this function
// A reference popped out of an array points where the element did (§9.2):
// into the function's own xs, which it may not outlive.
fn leak() -> i64& {
    var xs: i64[4] = [1, 2, 3, 4];
    var stack: i64&[>..<] = [];
    stack.push(xs[1]);
    return stack.pop();
}

fn main() {
    print(leak());
}
