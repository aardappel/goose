// error: extern fn zv: C symbol "zlib.Version" is not a C identifier
extern fn zv() -> u64 = "zlib.Version";
fn main() { print(zv()); }
