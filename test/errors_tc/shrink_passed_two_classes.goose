// error: cannot call consume: it shrinks values while r is still used, and r may refer into values
// A reference read back out of a holder only outlives what may own its
// target, so it gets a class of its own beside values': the callee cannot
// tell the two classes apart, and this call makes them one array (§5.1).
struct H { r: i64& }
fn consume(r: i64&, a: i64[>..]&) {
    a.clear();
    a.push(99);
    print(r);
}
fn main() {
    var values: i64[>..] = [8, 9];
    var other = 5;
    let h = H { values[0] };
    consume(h.r, values);
    print(other);
}
