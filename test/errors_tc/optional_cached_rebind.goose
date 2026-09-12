fn main() {
    var n = 41;
    var opt: i64? = n;
    fn clear() { opt .= null; }
    clear();
    opt .= n;
    assert(opt);
    clear();
    print(opt + 1);
}
