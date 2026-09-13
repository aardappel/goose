// error: operator + cannot be applied to i64? and i64
fn main() {
    var n = 41;
    var opt: i64? = n;
    fn clear() { opt .= null; }
    assert(opt);
    for i in 2 {
        print(opt + 1);
        clear();
        fn clear() { }
    }
}
