// error: extern fn zv: C symbol "zlib.Version" is not a C identifier
extern "zlib.Version" fn zv() -> u64;
fn main() { print(zv()); }
