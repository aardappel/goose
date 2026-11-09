// error: a global of type own Buf& would hold its obligation for good
// A global has no scope exit, and a non-optional handle no empty value
// take() could leave behind.
struct Buf { n: i64 }
var bufs: Buf[>..] = [Buf { n: 1 }];
var held: own Buf& = bufs[0];
fn main() {}
