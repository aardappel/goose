// error: the default of parameter y of inner names function value F
// outer's F, a function value, hides the global function F around inner's
// declaration, and a default calls no function value (§7.1).
fn F(x: i64) -> i64 { x * 100 }
fn g(x: i64) -> i64 { x + 1 }

fn outer<F>(x: i64) -> i64 {
    fn inner(y: i64 = F(1)) -> i64 { y + x }
    inner()
}

fn main() { print(outer(1, g)); }
