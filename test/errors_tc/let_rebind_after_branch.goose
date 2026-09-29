// error: cannot rebind let r, which may be bound already
// A let reference declared without a value is bound once (§3.8, §4.4).
fn run(c: bool) {
    var a = 1;
    var b = 2;
    let r: i64?;
    if c { r .= &a; }
    r .= &b;
    if r { print(r); }
}
fn main() { run(true); }
