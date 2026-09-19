// error: returning a reference into a temporary
// So is a variant literal (§9.2).
enum Msg { Nums { xs: i64[3] }, Empty }
fn view() -> const i64[:] { Msg.Nums { xs: [4, 5, 6] }.xs[..] }
fn main() { print(view()[0]); }
