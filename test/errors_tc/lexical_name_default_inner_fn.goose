// error: the default of parameter y of inner names function value F
// inner's own F, a function value, hides outer's F, bound to a type, in
// inner's default (§7.1, §11.1).
fn g(x: i64) -> i64 { x + 1 }

fn outer<F>(x: F) -> i64 {
    fn inner<F>(a: i64, y: i64 = F) -> i64 { F(a) + y }
    inner(1, g)
}

fn main() { print(outer(5)); }
