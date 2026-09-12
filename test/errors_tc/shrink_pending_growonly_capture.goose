// A recursive call still being checked counts as shrinking the grow-only
// arrays of its lexical parents that its body textually shrinks.
fn read(r: i64&, unused: i64) -> i64 { return r; }
fn outer() {
    var buf: i64[>..] = [1, 2];
    recursive fn step(n: i64) -> i64 {
        if n > 0 { return read(buf[0], step(n - 1)); }
        buf.clear();
        buf.push(99);
        return 0;
    }
    let v = step(1);
    print(v);
}
fn main() { outer(); }
