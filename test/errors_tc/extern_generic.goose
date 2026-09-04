// extern fns are fully typed and not generic (§7.10).
extern fn ident<T>(x: T) -> T;
fn main() { print(ident(1)); }
