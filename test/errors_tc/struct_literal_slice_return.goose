// error: returning a reference into a temporary
// A struct literal viewed through one of its fields is a temporary too
// (§9.2).
struct Box { a: i64[3] }
fn view() -> const i64[:] { Box { a: [1, 2, 3] }.a[..] }
fn main() { print(view()[0]); }
