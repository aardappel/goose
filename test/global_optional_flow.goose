var n = 41;
var opt: i64? = null;
fn main() {
    if n == 41 { opt .= n; } else { opt .= n; }
    print(opt + 1);
    while opt {
        print(opt + 2);
        opt .= null;
    }
    if opt { print(0); } else { print(44); }
}
