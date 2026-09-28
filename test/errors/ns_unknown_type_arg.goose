// error: unknown type: lim::Missing
// A type argument list that a call follows stays one, so an unknown qualified
// name in it is an unknown type. The comparison around the call tried the
// same name as a type argument first, and took it back
// (syntax/namespace_comparisons.goose).
fn id<T>(x: T) -> T { x }
fn main() { let a = 1; if a < id<lim::Missing>(a) { print(a); } }
