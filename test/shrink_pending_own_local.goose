// A function's own array local hides an outer array of the same name for
// the rest of its block: clearing it shrinks neither a lexical parent's
// local nor a global, whatever calls are still being checked.
var log: u8[>..] = [];
fn read(r: i64&, unused: i64) -> i64 { return r; }
fn outer() {
    var buf: i64[>..] = [1, 2];
    recursive fn step(n: i64) -> i64 {
        if n > 0 { return read(buf[0], step(n - 1)); }
        var buf: i64[..4] = [3];
        buf.clear();
        return buf.len;
    }
    let v = step(1);
    print(v, " ", buf);
}
recursive fn fib(n: i64) -> i64 {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn main() {
    outer();
    var log: u8[>..] = [1];
    print(fib(10));
    log.clear();
    print(log.len);
}
