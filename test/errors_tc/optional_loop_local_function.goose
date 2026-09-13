// error: operator + cannot be applied to i64? and i64
fn main() {
    var n = 41;
    var opt: i64? = n;
    assert(opt);
    for i in 2 {
        fn clear() { opt .= null; }
        print(opt + 1);
        clear();
    }
}
