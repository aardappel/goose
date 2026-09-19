// error: cannot call doubled in the value assigned to x: doubled uses r, which may refer to x
// A reference to the whole array survives the shrink the assignment starts
// with (§5.1), and reaches the new contents under construction.
fn main() {
    var x: i64[>..] = [1, 2, 3];
    let r = x;
    fn doubled() -> i64[>..] {
        var out: i64[>..] = [];
        for v in r { out.push(v); }
        for v in r { out.push(v); }
        return out;
    }
    x = doubled();
    print(x);
}
