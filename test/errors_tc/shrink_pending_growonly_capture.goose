// error: cannot call step, which shrinks buf: buf[0], evaluated earlier in the statement
// error: may still refer into buf
// A recursive call applies what the cycle's first round recorded (§7.8):
// the shrink of a grow-only array of its lexical parent.
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
