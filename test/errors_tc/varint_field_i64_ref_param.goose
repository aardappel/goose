// error: no matching overload for call to setto
// A varint field is storage of type varint (§3.6), which an i64& parameter
// does not bind however the field reads ("cannot pass varint as i64&"):
// through the reference the callee would read and write the encoded bytes
// as an i64.
struct V { n: varint, w: i64 }
fn setto(r: i64&) { r = 7; }
fn main() {
    var v = V { n: 300, w: 4 };
    setto(v.n);
    print(v);
}
