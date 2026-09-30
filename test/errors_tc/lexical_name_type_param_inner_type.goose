// error: type parameter F is bound to the type i64, not a function
// Inside inner its own F, bound to a type, hides outer's F, a function value
// (§11.1).
fn g(x: i64) -> i64 { x + 1 }

fn outer<F>(x: i64) -> i64 {
    fn inner<F>(y: F) -> i64 { F(y) }
    inner(x)
}

fn main() { print(outer(2, g)); }
