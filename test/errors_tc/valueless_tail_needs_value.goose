// error: function g must return value(s)
// Looking through a block for a tail that produces nothing does not excuse a
// function that declared a result: the block falls off its end, so g returns
// on no path, and the diagnostic is the one a bare `if` tail already gives.
fn g(n: i64) -> i64 { block { if n > 0 { print(n); } } }
fn main() { print(g(1)); }
