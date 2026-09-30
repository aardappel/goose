// error: expected a value of type u8[:]&, got u8[:]
// Nor does a reference field.
var h: u8[>..] = [];
struct Named { rs: u8[:]& }
fn mk<F>() -> Named { Named { rs: F() } }
fn main() {
    h.append("abcd");
    var s = h[..];
    let n = mk() { s };
    print(n.rs.len);
}
