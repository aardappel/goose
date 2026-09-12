// A lexical parent's local named like a global: a recursive call that
// textually clears it counts as shrinking the local.
var buf: i64[>..] = [7];
fn outer() {
    var buf: i64[>..] = [1, 2];
    recursive fn step(n: i64) -> i64 {
        if n > 0 {
            let r .= buf[0];
            let z = step(n - 1);
            return r + z;
        }
        buf.clear();
        buf.push(99);
        return 0;
    }
    let v = step(1);
    print(v, " ", buf);
}
fn main() {
    outer();
    print(buf);
}
