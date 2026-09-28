// error: storing a reference rooted at x, which does not outlive the destination
// A function no call reaches is checked with a caller's array behind each
// reference parameter, which outlives the function's own locals, so storing a
// reference to one of those there is an error, as it is for any call.
struct Box { r: i64& }
fn stash(dst: Box[>..]&) { var x = 5; dst.push(Box { r: x }); }
fn main() { print(1); }
