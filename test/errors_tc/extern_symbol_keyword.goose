// error: extern fn f: C symbol int is a C keyword
extern "int" fn f() -> i32;
fn main() { print(f()); }
