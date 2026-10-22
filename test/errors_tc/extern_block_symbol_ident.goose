// error: extern fn x: C symbol "my sym" is not a C identifier
extern "z" { fn x() = "my sym"; }
fn main() { x(); }
