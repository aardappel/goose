// error: field b of variant Slot.Full holds an own value (own Buf?): an enum's payloads cannot own anything
// A match binds a payload by value and an assignment replaces a variant,
// either of which would copy or drop what the payload owns.
struct Buf { n: i64 }
enum Slot { Full { b: own Buf? }, Empty }
fn main() {}
