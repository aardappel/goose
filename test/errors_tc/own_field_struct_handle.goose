// error: field w of struct Game has type own Window, an own value with no empty state
// A struct that is itself the claim has no empty value either.
struct Window { id: u64 }
struct Game { w: own Window }
fn main() {}
