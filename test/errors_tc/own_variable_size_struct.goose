// error: struct Named holds own values, so it must be fixed-size
// take() and replace() exchange an own field where it lies, as a member of
// a struct with a layout of its own.
struct Buf { n: i64 }
struct Named { name: u8[], b: own Buf? }
fn main() {}
