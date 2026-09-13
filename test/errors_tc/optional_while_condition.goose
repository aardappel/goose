// error: operator + cannot be applied to i64? and i64
// A while condition runs again after the body, so a rebind in the body
// un-narrows it too.
var n = 41;
var opt: i64? = null;
fn main() {
    opt .= n;
    var i = 0;
    while opt + i < 43 {
        i++;
        opt .= null;
    }
    print(i);
}
