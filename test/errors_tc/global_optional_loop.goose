var n = 41;
var opt: i64? = null;
fn main() {
    opt .= n;
    for i in 2 {
        print(opt + 1);
        opt .= null;
    }
}
