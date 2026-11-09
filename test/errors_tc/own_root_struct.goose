// error: own qualifies a reference, a slice, a struct or an integer handle, not Bag
// A root struct is its declaring variable for its whole life.
root struct Bag { items: i64[>..] = [] }
fn g(b: own Bag) {}
fn main() {}
