// error: condition must be bool or an optional, got i64
// A clause is a condition, checked as one.
fn f(x: i64) -> i64
    requires x + 1
{
    x
}

fn main() {
    print(f(3));
}
