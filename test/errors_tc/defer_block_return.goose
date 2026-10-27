// error: return cannot leave a defer body (§6.6)
// A return in a function value written in the body exits the enclosing
// function, through the call running the value, so it leaves the body too.
fn each<F>(xs: i64[:]) { for x in xs { F(x); } }
fn f() -> i64 {
    defer each([1, 2]) { if it == 2 { return it; } };
    return 0;
}
fn main() { print(f()); }
