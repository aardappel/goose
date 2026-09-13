// A call back into a function still being checked rebinds only the optionals
// a `.=` in the code it can run names: recursion that rebinds nothing keeps
// narrowings around its calls, inside loops and after them.
var n = 41;
var g: i64? = null;
recursive fn count(k: i64) -> i64 {
    var s = 0;
    for i in 2 { if k > 0 { s += count(k - 1); } }
    var j = 0;
    while j < 2 { if k > 0 { s += count(k - 1); } j++; }
    loop { if k > 0 { s += count(k - 1); } break; }
    return s + 1;
}
recursive fn countdown<F>(k: i64) {
    for i in 2 { if k > 0 { countdown(k - 1, F); } }
    F(k);
}
recursive fn walk<F>(k: i64) {
    if k > 0 { walk(k - 1, F); walk(k - 1, F); }
    F(k);
}
fn main() {
    g .= n;
    print(count(2));
    print(g + 1);
    var m = 1;
    var opt: i64? = m;
    if opt {
        countdown(1) { print(it); }
        walk(1) { print(it + opt); }
        print(opt + 1);
    }
}
