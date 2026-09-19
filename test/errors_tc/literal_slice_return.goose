// error: returning a reference into a temporary
// An array literal that is viewed rather than built into a destination is a
// temporary of its statement: a slice of it dies with the function (§9.2).
fn view() -> const i64[:] { [1, 2, 3][..] }
fn main() { print(view()[0]); }
