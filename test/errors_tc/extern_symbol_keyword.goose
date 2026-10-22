// error: extern fn f: C symbol int is a C keyword
extern fn f() -> i32 = "int";
fn main() { print(f()); }
