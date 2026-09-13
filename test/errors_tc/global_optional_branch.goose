// error: operator + cannot be applied to i64? and i64
var n = 41;
var opt: i64? = null;
fn main() {
    if false { opt .= n; }
    print(opt + 1);
}
