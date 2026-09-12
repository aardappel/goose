fn main() {
    var n = 41;
    var opt: i64? = n;
    fn clear() { opt .= null; }
    assert(opt);
    for i in 2 {
        print(opt + 1);
        clear();
    }
}
