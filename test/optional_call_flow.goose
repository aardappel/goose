fn clear() { }
fn global_invoke() { clear(); }

fn main() {
    var n = 41;
    var opt: i64? = n;
    fn use() { print(opt + 1); }
    fn clear() { opt .= null; }
    fn clear_twice() { clear(); clear(); }
    if opt { use(); }
    clear_twice();
    opt .= n;
    if opt { use(); }
    clear_twice();
    if opt { print(0); } else { print(43); }
    opt .= n;
    while opt {
        use();
        clear();
    }
    opt .= n;
    assert(opt);
    for i in 2 {
        fn unused_clear() { opt .= null; }
        print(opt + 1);
    }
    for i in 2 {
        fn clear() { }
        print(opt + 1);
        clear();
    }
    for i in 2 {
        print(opt + 1);
        global_invoke();
    }
}
