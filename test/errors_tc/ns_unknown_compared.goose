// error: unknown identifier: lim::Missing
// A `<` that starts no type argument list is a comparison, so the qualified
// name after it is looked up as a value, which the checker reports unknown
// (syntax/namespace_comparisons.goose).
fn main() { let a = 1; if a < lim::Missing { print(a); } }
