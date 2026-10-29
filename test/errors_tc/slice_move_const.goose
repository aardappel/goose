// error: cannot .move_from through a non-writable value
fn main() { "abc".move_from("def"); }
