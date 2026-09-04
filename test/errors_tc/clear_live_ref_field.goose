// A struct holding a reference into the buffer is live at the clear (§5.1);
// where the reference itself came from is not tracked past the store.
struct Hold { r: u8& }
fn main() {
    var buf: u8[>..] = [];
    buf.push(1);
    var h = Hold { r: buf[0] };
    buf.clear();
    print(h.r);
}
