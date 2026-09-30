// error: cannot pop arr: the for loop at
// error: iterates wrap(arr), whose elements may refer into arr
// The loop walks the call's result in place, whose elements hold references
// into arr, while its body pops arr (§5.1).
struct H { r: i64& }
fn wrap(a: i64[>..]&) -> H[] { [H { r: a[0] }] }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    for h in wrap(arr) {
        arr.pop(); arr.pop(); arr.pop();
        arr.push(42);
        print(h.r);
    }
}
