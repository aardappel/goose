// error: cannot call stamp, which grows b: the value assigned to b is still under construction
// The new contents of a value holding a resizable array are built in place
// as well, so nothing may grow that array meanwhile (§4.2).
struct Box { tag: i64, items: u8[>..] }
fn main() {
    var b = Box { tag: 1, items: [] };
    fn stamp() -> i64 {
        b.items.push(1);
        return 2;
    }
    b = Box { tag: stamp(), items: [] };
    print(b.tag);
}
