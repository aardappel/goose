// error: Pair<i64> is not an enum: it has no variant None
// `Name<T>.Variant` names a variant of a generic enum (§3.5).
struct Pair<T> { a: T, b: T }
fn main() { let p = Pair<i64>.None; print(p); }
