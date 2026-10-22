// error: extern fn f names its symbol twice, as "a" and "b": write only = "b"
// The deprecated symbol before `fn` and the one after the signature. (spec §7.10)
extern "a" fn f() = "b";
fn main() {}
