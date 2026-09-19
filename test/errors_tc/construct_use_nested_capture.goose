// error: cannot call rebuilt in the value assigned to x: rebuilt uses x
// A nested function reads the local it captures.
fn main() {
    var x: i64[>..] = [1, 2, 3];
    fn rebuilt() -> i64[>..] {
        var out: i64[>..] = [];
        for v in x { out.push(v * 10); }
        for v in x { out.push(v); }
        return out;
    }
    x = rebuilt();
    print(x);
}
