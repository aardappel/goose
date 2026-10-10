// error: cannot clear buf while got is still used
// error: it was read out of refs and may point into other or buf
// A `for` binder over an array of references points where the stores into
// the array put them (§9.5): got may point into buf after the loop.
fn main() {
    var buf: i64[>..] = [];
    buf.push(7);
    var other: i64[1] = [0];
    var got: i64& = other[0];
    var refs: i64&[..4] = [];
    refs.push(buf[0]);
    for r in refs { got .= r; }
    buf.clear();
    buf.push(99);
    print(got);
}
