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
