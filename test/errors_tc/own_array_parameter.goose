// error: an array of own-holding elements (Slot[>..<]) is not passed by value
struct Buf { n: i64 }
struct Slot { b: own Buf? }
fn eat(xs: Slot[>..<]) {}
fn main() {}
