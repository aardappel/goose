// error: extern fn x: C symbol "my sym" is not a C identifier
extern "z" { "my sym" fn x(); }
fn main() { x(); }
