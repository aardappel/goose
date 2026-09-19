// error: returning a reference into a temporary
// A call's result ends with the statement that made the call, and a return
// ends the function.
fn mk() -> u8[] { "hello" }
fn view() -> const u8[:] { mk()[1..] }
fn main() { print(view()); }
