// error: ambiguous call to root: multiple overloads match equally well
// An integer converts to either float type equally well (§7.1): which
// precision the call computes at is the program's to say, `root(n as f64)`.
fn root(x: f32) -> f32 { x * 0.5 }
fn root(x: f64) -> f64 { x * 0.5 }
fn main() {
    var n: i32 = 9;
    print(root(n));
}
