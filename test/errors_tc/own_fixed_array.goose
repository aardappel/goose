// error: an array of fixed length cannot hold own values (own Buf?)
// Own values enter and leave an array one at a time, by push and pop.
struct Buf { n: i64 }
struct Table { slots: (own Buf?)[4] }
fn main() {}
