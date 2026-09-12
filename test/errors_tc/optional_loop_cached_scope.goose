fn main() {
    var n = 41;
    var opt: i64? = n;
    fn clear() { opt .= null; }
    fn invoke() { clear(); }
    if opt { invoke(); }
    opt .= n;
    if opt {
        for i in 2 {
            fn clear() { }
            print(opt + 1);
            invoke();
        }
    }
}
