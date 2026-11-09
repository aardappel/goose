// error: an array of fixed length cannot hold own values (Slot)
// A T[] is sized once and never changes, so nothing could leave it.
struct Buf { n: i64 }
struct Slot { b: own Buf? }
fn f(xs: Slot[]&) {}
fn main() {}
