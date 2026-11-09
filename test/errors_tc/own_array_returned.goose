// error: an array of own-holding elements (Slot[>..<]) is not returned by value
// One is the variable declaring it for its whole life.
struct Buf { n: i64 }
struct Slot { b: own Buf? }
fn make() -> Slot[>..<] { [] }
fn main() {}
