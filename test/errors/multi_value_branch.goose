// error: several values exist only in `return a, b;` and in a call's results
fn f(focus: bool) -> i64, i64 {
    return if focus { 500, 100 } else { 250, 50 };
}
fn main() { let a, b = f(true); print(a, b); }
