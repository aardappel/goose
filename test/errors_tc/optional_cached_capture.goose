// error: operator + cannot be applied to i64? and i64
fn main() {
    var n = 41;
    var opt: i64? = n;
    fn use() { print(opt + 1); }
    if opt { use(); }
    opt .= null;
    use();
}
