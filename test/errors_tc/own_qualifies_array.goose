// error: own qualifies a reference, a slice, a struct or an integer handle, not u8[>..]
// An array owns its elements already, as the variable declaring it.
fn f(xs: own u8[>..]) {}
fn main() {}
